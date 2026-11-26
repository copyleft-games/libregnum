/* lrg-touch-tracker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Touch gesture recognition.  The tracker keeps the fingers that are down
 * (in the order they went down) and, per feed, works in three passes:
 * movement of the fingers that stayed, lifts (taps, two-finger taps, the
 * ends of drags and pinches), new fingers (a second finger turns a drag
 * into a pinch candidate); then the continuous gestures (drag, pinch,
 * long press) from the resulting state.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <graylib.h>

#include "lrg-touch-tracker.h"

/* ==========================================================================
 * Boxed value types
 * ========================================================================== */

/* lrg_touch_point_copy: documented in lrg-touch-tracker.h */
LrgTouchPoint *
lrg_touch_point_copy (const LrgTouchPoint *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return g_memdup2 (self, sizeof (LrgTouchPoint));
}

/* lrg_touch_point_free: documented in lrg-touch-tracker.h */
void
lrg_touch_point_free (LrgTouchPoint *self)
{
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgTouchPoint, lrg_touch_point, lrg_touch_point_copy, lrg_touch_point_free)

/* lrg_touch_gesture_copy: documented in lrg-touch-tracker.h */
LrgTouchGesture *
lrg_touch_gesture_copy (const LrgTouchGesture *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return g_memdup2 (self, sizeof (LrgTouchGesture));
}

/* lrg_touch_gesture_free: documented in lrg-touch-tracker.h */
void
lrg_touch_gesture_free (LrgTouchGesture *self)
{
	g_free (self);
}

G_DEFINE_BOXED_TYPE (LrgTouchGesture, lrg_touch_gesture, lrg_touch_gesture_copy, lrg_touch_gesture_free)

G_DEFINE_ENUM_TYPE (LrgTouchGestureType, lrg_touch_gesture_type,
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_GESTURE_TAP, "tap"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_GESTURE_DOUBLE_TAP, "double-tap"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_GESTURE_LONG_PRESS, "long-press"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_GESTURE_DRAG_BEGIN, "drag-begin"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_GESTURE_DRAG, "drag"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_GESTURE_DRAG_END, "drag-end"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_GESTURE_PINCH_BEGIN, "pinch-begin"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_GESTURE_PINCH, "pinch"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_GESTURE_PINCH_END, "pinch-end"),
	G_DEFINE_ENUM_VALUE (LRG_TOUCH_GESTURE_TWO_FINGER_TAP, "two-finger-tap"))

/* ==========================================================================
 * The tracker
 * ========================================================================== */

/* raylib's MAX_TOUCH_POINTS */
#define MAX_DEVICE_POINTS (10)

/* the smallest finger distance a pinch scale divides by, in pixels */
#define MIN_PINCH_DISTANCE (1.0f)

typedef struct
{
	gint     id;
	gfloat   start_x;
	gfloat   start_y;
	gfloat   x;
	gfloat   y;
	gfloat   prev_x;
	gfloat   prev_y;
	gdouble  down_time;
	gboolean moved;       /* past the tap slop once: never a tap again */
	gboolean long_fired;  /* a long press was reported: its lift is no tap */
	gboolean multi;       /* part of a two-or-more-finger gesture */
	gboolean seen;        /* present in the current feed */
} Finger;

struct _LrgTouchTracker
{
	GObject parent_instance;

	GArray *fingers;    /* Finger, in the order they went down */
	GArray *gestures;   /* LrgTouchGesture, the last feed's */

	/* thresholds */
	gdouble tap_max_duration;
	gfloat  tap_slop;             /* dp */
	gdouble double_tap_interval;
	gfloat  double_tap_slop;      /* dp */
	gdouble long_press_duration;
	gfloat  dp_scale;

	/* one-finger drag */
	gboolean dragging;
	gint     drag_finger;

	/* two fingers: a pinch once they move */
	gboolean pair;                /* fingers pair_a and pair_b are down together */
	gboolean pinching;
	gint     pair_a;
	gint     pair_b;
	gfloat   pinch_start_distance;
	gfloat   pinch_prev_distance;
	gfloat   pinch_start_cx;
	gfloat   pinch_start_cy;
	gfloat   pinch_prev_cx;
	gfloat   pinch_prev_cy;
	gdouble  pair_down_time;

	/* two-finger tap: both lift before moving or pinching */
	gboolean two_tap_possible;
	guint    two_tap_lifted;

	/* the previous tap, for double taps */
	gboolean have_last_tap;
	gdouble  last_tap_time;
	gfloat   last_tap_x;
	gfloat   last_tap_y;
};

enum
{
	PROP_0,
	PROP_TAP_MAX_DURATION,
	PROP_TAP_SLOP,
	PROP_DOUBLE_TAP_INTERVAL,
	PROP_DOUBLE_TAP_SLOP,
	PROP_LONG_PRESS_DURATION,
	PROP_DP_SCALE,
	N_PROPS
};

enum
{
	SIGNAL_GESTURE,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgTouchTracker, lrg_touch_tracker, G_TYPE_OBJECT)

static void
lrg_touch_tracker_finalize (GObject *object)
{
	LrgTouchTracker *self = LRG_TOUCH_TRACKER (object);

	g_clear_pointer (&self->fingers, g_array_unref);
	g_clear_pointer (&self->gestures, g_array_unref);

	G_OBJECT_CLASS (lrg_touch_tracker_parent_class)->finalize (object);
}

static void
lrg_touch_tracker_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgTouchTracker *self = LRG_TOUCH_TRACKER (object);

	switch (prop_id)
	{
	case PROP_TAP_MAX_DURATION:
		g_value_set_double (value, self->tap_max_duration);
		break;
	case PROP_TAP_SLOP:
		g_value_set_float (value, self->tap_slop);
		break;
	case PROP_DOUBLE_TAP_INTERVAL:
		g_value_set_double (value, self->double_tap_interval);
		break;
	case PROP_DOUBLE_TAP_SLOP:
		g_value_set_float (value, self->double_tap_slop);
		break;
	case PROP_LONG_PRESS_DURATION:
		g_value_set_double (value, self->long_press_duration);
		break;
	case PROP_DP_SCALE:
		g_value_set_float (value, self->dp_scale);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_touch_tracker_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgTouchTracker *self = LRG_TOUCH_TRACKER (object);

	switch (prop_id)
	{
	case PROP_TAP_MAX_DURATION:
		self->tap_max_duration = g_value_get_double (value);
		break;
	case PROP_TAP_SLOP:
		self->tap_slop = g_value_get_float (value);
		break;
	case PROP_DOUBLE_TAP_INTERVAL:
		self->double_tap_interval = g_value_get_double (value);
		break;
	case PROP_DOUBLE_TAP_SLOP:
		self->double_tap_slop = g_value_get_float (value);
		break;
	case PROP_LONG_PRESS_DURATION:
		self->long_press_duration = g_value_get_double (value);
		break;
	case PROP_DP_SCALE:
		self->dp_scale = g_value_get_float (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_touch_tracker_class_init (LrgTouchTrackerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_touch_tracker_finalize;
	object_class->get_property = lrg_touch_tracker_get_property;
	object_class->set_property = lrg_touch_tracker_set_property;

	/**
	 * LrgTouchTracker:tap-max-duration:
	 *
	 * The longest a finger may stay down and still tap, in seconds.  Two
	 * fingers tap together within twice this.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_TAP_MAX_DURATION] =
		g_param_spec_double ("tap-max-duration", "Tap max duration",
		                     "Longest touch that is a tap (s)",
		                     0.0, 10.0, 0.3,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgTouchTracker:tap-slop:
	 *
	 * How far a finger may move, in dp, and still tap or long-press;
	 * moving further starts a drag.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_TAP_SLOP] =
		g_param_spec_float ("tap-slop", "Tap slop",
		                    "Movement that still taps (dp)",
		                    0.0f, 1000.0f, 10.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgTouchTracker:double-tap-interval:
	 *
	 * The longest time between two taps' lifts that makes a double tap,
	 * in seconds.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_DOUBLE_TAP_INTERVAL] =
		g_param_spec_double ("double-tap-interval", "Double tap interval",
		                     "Longest time between two taps of a double tap (s)",
		                     0.0, 10.0, 0.3,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgTouchTracker:double-tap-slop:
	 *
	 * The farthest apart two taps of a double tap may be, in dp.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_DOUBLE_TAP_SLOP] =
		g_param_spec_float ("double-tap-slop", "Double tap slop",
		                    "Farthest apart the taps of a double tap may be (dp)",
		                    0.0f, 1000.0f, 40.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgTouchTracker:long-press-duration:
	 *
	 * How long a finger is held still before it is a long press, in
	 * seconds.
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_LONG_PRESS_DURATION] =
		g_param_spec_double ("long-press-duration", "Long press duration",
		                     "Hold time of a long press (s)",
		                     0.0, 60.0, 0.5,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgTouchTracker:dp-scale:
	 *
	 * Pixels per dp: the slops are multiplied by it.  Set it from
	 * lrg_platform_get_dp_scale().
	 *
	 * Since: 0.13.0
	 */
	properties[PROP_DP_SCALE] =
		g_param_spec_float ("dp-scale", "dp scale",
		                    "Pixels per dp",
		                    0.01f, 100.0f, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgTouchTracker::gesture:
	 * @self: the #LrgTouchTracker
	 * @gesture: the #LrgTouchGesture
	 *
	 * A gesture was recognised.  Emitted from lrg_touch_tracker_feed(),
	 * after the whole frame was processed.
	 *
	 * Since: 0.13.0
	 */
	signals[SIGNAL_GESTURE] =
		g_signal_new ("gesture", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1,
		              LRG_TYPE_TOUCH_GESTURE | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
lrg_touch_tracker_init (LrgTouchTracker *self)
{
	self->fingers = g_array_new (FALSE, TRUE, sizeof (Finger));
	self->gestures = g_array_new (FALSE, TRUE, sizeof (LrgTouchGesture));
	self->tap_max_duration = 0.3;
	self->tap_slop = 10.0f;
	self->double_tap_interval = 0.3;
	self->double_tap_slop = 40.0f;
	self->long_press_duration = 0.5;
	self->dp_scale = 1.0f;
}

/* lrg_touch_tracker_new: documented in lrg-touch-tracker.h */
LrgTouchTracker *
lrg_touch_tracker_new (void)
{
	return g_object_new (LRG_TYPE_TOUCH_TRACKER, NULL);
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gfloat
distance (gfloat ax,
          gfloat ay,
          gfloat bx,
          gfloat by)
{
	return sqrtf ((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
}

static Finger *
find_finger (LrgTouchTracker *self,
             gint             id)
{
	guint i;

	for (i = 0; i < self->fingers->len; i++)
	{
		Finger *f = &g_array_index (self->fingers, Finger, i);

		if (f->id == id)
			return f;
	}
	return NULL;
}

/* push: a gesture with the defaults filled in (no movement, scale 1) */
static LrgTouchGesture *
push (LrgTouchTracker     *self,
      LrgTouchGestureType  type,
      gint                 finger,
      gfloat               x,
      gfloat               y,
      gfloat               start_x,
      gfloat               start_y,
      gdouble              duration)
{
	LrgTouchGesture g;

	g.type = type;
	g.finger = finger;
	g.x = x;
	g.y = y;
	g.start_x = start_x;
	g.start_y = start_y;
	g.dx = 0.0f;
	g.dy = 0.0f;
	g.scale = 1.0f;
	g.total_scale = 1.0f;
	g.duration = duration;
	g_array_append_val (self->gestures, g);
	return &g_array_index (self->gestures, LrgTouchGesture, self->gestures->len - 1);
}

/* pair_centre: two fingers' centre and distance */
static void
pair_centre (const Finger *a,
             const Finger *b,
             gfloat       *cx,
             gfloat       *cy,
             gfloat       *dist)
{
	*cx = (a->x + b->x) * 0.5f;
	*cy = (a->y + b->y) * 0.5f;
	*dist = MAX (distance (a->x, a->y, b->x, b->y), MIN_PINCH_DISTANCE);
}

/* tap: a single tap, and a double tap when the previous one was close */
static void
tap (LrgTouchTracker *self,
     const Finger    *f,
     gdouble          time)
{
	gfloat double_slop = self->double_tap_slop * self->dp_scale;

	push (self, LRG_TOUCH_GESTURE_TAP, f->id, f->x, f->y, f->start_x, f->start_y,
	      time - f->down_time);
	if (self->have_last_tap &&
	    time - self->last_tap_time <= self->double_tap_interval &&
	    distance (self->last_tap_x, self->last_tap_y, f->x, f->y) <= double_slop)
	{
		push (self, LRG_TOUCH_GESTURE_DOUBLE_TAP, f->id, f->x, f->y,
		      self->last_tap_x, self->last_tap_y, time - self->last_tap_time);
		/* a third tap starts over rather than doubling again */
		self->have_last_tap = FALSE;
		return;
	}
	self->have_last_tap = TRUE;
	self->last_tap_time = time;
	self->last_tap_x = f->x;
	self->last_tap_y = f->y;
}

/* lift: one finger left the screen */
static void
lift (LrgTouchTracker *self,
      const Finger    *f,
      gdouble          time)
{
	gboolean in_pair = self->pair && (f->id == self->pair_a || f->id == self->pair_b);

	if (self->dragging && self->drag_finger == f->id)
	{
		push (self, LRG_TOUCH_GESTURE_DRAG_END, f->id, f->x, f->y, f->start_x, f->start_y,
		      time - f->down_time);
		self->dragging = FALSE;
	}

	if (in_pair)
	{
		/* the other finger may have lifted in this same frame: the pair's
		 * last known centre, not the fingers */
		if (self->pinching)
		{
			LrgTouchGesture *g;

			g = push (self, LRG_TOUCH_GESTURE_PINCH_END, self->pair_a,
			          self->pinch_prev_cx, self->pinch_prev_cy,
			          self->pinch_start_cx, self->pinch_start_cy, time - self->pair_down_time);
			g->total_scale = self->pinch_prev_distance / self->pinch_start_distance;
		}
		if (self->two_tap_possible)
		{
			self->two_tap_lifted++;
			if (self->two_tap_lifted == 2)
			{
				if (time - self->pair_down_time <= self->tap_max_duration * 2.0)
					push (self, LRG_TOUCH_GESTURE_TWO_FINGER_TAP, self->pair_a,
					      self->pinch_start_cx, self->pinch_start_cy,
					      self->pinch_start_cx, self->pinch_start_cy,
					      time - self->pair_down_time);
				self->two_tap_possible = FALSE;
			}
		}
		/* the pair is over; the other finger, if still down, is not a new
		 * gesture (it stays "multi") but can tap-pair again once a new
		 * finger comes */
		self->pinching = FALSE;
		if (!self->two_tap_possible)
			self->pair = FALSE;
		return;
	}

	if (!f->multi && !f->moved && !f->long_fired && time - f->down_time <= self->tap_max_duration)
		tap (self, f, time);
}

/* ==========================================================================
 * Feeding
 * ========================================================================== */

/* lrg_touch_tracker_feed: documented in lrg-touch-tracker.h */
void
lrg_touch_tracker_feed (LrgTouchTracker     *self,
                        const LrgTouchPoint *points,
                        guint                n_points,
                        gdouble              time)
{
	g_autoptr(GArray) emitted = NULL;
	Finger *pair_a;
	Finger *pair_b;
	gfloat slop;
	guint first_new;
	guint i;

	g_return_if_fail (LRG_IS_TOUCH_TRACKER (self));
	g_return_if_fail (points != NULL || n_points == 0);

	g_array_set_size (self->gestures, 0);
	slop = self->tap_slop * self->dp_scale;

	/* 1. who stayed and where they are; new fingers go to the end */
	for (i = 0; i < self->fingers->len; i++)
		g_array_index (self->fingers, Finger, i).seen = FALSE;
	first_new = self->fingers->len;
	for (i = 0; i < n_points; i++)
	{
		Finger *f = find_finger (self, points[i].id);

		if (f != NULL)
		{
			if (f->seen)
				continue;   /* the same id twice: the first counts */
			f->prev_x = f->x;
			f->prev_y = f->y;
			f->x = points[i].x;
			f->y = points[i].y;
			f->seen = TRUE;
			if (!f->moved && distance (f->start_x, f->start_y, f->x, f->y) > slop)
				f->moved = TRUE;
		}
		else
		{
			Finger nf;

			memset (&nf, 0, sizeof nf);
			nf.id = points[i].id;
			nf.start_x = nf.x = nf.prev_x = points[i].x;
			nf.start_y = nf.y = nf.prev_y = points[i].y;
			nf.down_time = time;
			nf.seen = TRUE;
			g_array_append_val (self->fingers, nf);
		}
	}

	/* 2. lifts, in the order the fingers went down */
	for (i = 0; i < first_new; )
	{
		Finger *f = &g_array_index (self->fingers, Finger, i);

		if (f->seen)
		{
			i++;
			continue;
		}
		lift (self, f, time);
		g_array_remove_index (self->fingers, i);
		first_new--;
	}

	/* 3. new fingers: the second finger down pairs with the first */
	for (i = first_new; i < self->fingers->len; i++)
	{
		Finger *f = &g_array_index (self->fingers, Finger, i);

		if (i == 1 && !self->pair)
		{
			Finger *a = &g_array_index (self->fingers, Finger, 0);
			gfloat cx, cy, dist;

			if (self->dragging)
			{
				push (self, LRG_TOUCH_GESTURE_DRAG_END, a->id, a->x, a->y, a->start_x, a->start_y,
				      time - a->down_time);
				self->dragging = FALSE;
			}
			a->multi = TRUE;
			f->multi = TRUE;
			self->pair = TRUE;
			self->pinching = FALSE;
			self->pair_a = a->id;
			self->pair_b = f->id;
			self->pair_down_time = a->down_time;
			self->two_tap_possible = !a->moved && !a->long_fired;
			self->two_tap_lifted = 0;
			pair_centre (a, f, &cx, &cy, &dist);
			self->pinch_start_distance = self->pinch_prev_distance = dist;
			self->pinch_start_cx = self->pinch_prev_cx = cx;
			self->pinch_start_cy = self->pinch_prev_cy = cy;
		}
		else if (i > 0)
		{
			/* a third finger (or one joining a pair still lifting): no
			 * two-finger tap, no single taps */
			f->multi = TRUE;
			self->two_tap_possible = FALSE;
			g_array_index (self->fingers, Finger, 0).multi = TRUE;
		}
	}

	/* 4a. one finger alone: drag and long press */
	if (self->fingers->len == 1)
	{
		Finger *f = &g_array_index (self->fingers, Finger, 0);

		if (!f->multi)
		{
			if (f->moved && !self->dragging)
			{
				LrgTouchGesture *g;

				self->dragging = TRUE;
				self->drag_finger = f->id;
				g = push (self, LRG_TOUCH_GESTURE_DRAG_BEGIN, f->id, f->x, f->y,
				          f->start_x, f->start_y, time - f->down_time);
				g->dx = f->x - f->start_x;
				g->dy = f->y - f->start_y;
			}
			else if (self->dragging && (f->x != f->prev_x || f->y != f->prev_y))
			{
				LrgTouchGesture *g;

				g = push (self, LRG_TOUCH_GESTURE_DRAG, f->id, f->x, f->y,
				          f->start_x, f->start_y, time - f->down_time);
				g->dx = f->x - f->prev_x;
				g->dy = f->y - f->prev_y;
			}
			else if (!f->moved && !f->long_fired &&
			         time - f->down_time >= self->long_press_duration)
			{
				f->long_fired = TRUE;
				push (self, LRG_TOUCH_GESTURE_LONG_PRESS, f->id, f->x, f->y,
				      f->start_x, f->start_y, time - f->down_time);
			}
		}
	}

	/* 4b. two paired fingers still down: a pinch once they move */
	pair_a = self->pair ? find_finger (self, self->pair_a) : NULL;
	pair_b = self->pair ? find_finger (self, self->pair_b) : NULL;
	if (pair_a != NULL && pair_b != NULL)
	{
		Finger *a = pair_a;
		Finger *b = pair_b;
		gfloat cx, cy, dist;

		pair_centre (a, b, &cx, &cy, &dist);
		if (a->moved || b->moved)
			self->two_tap_possible = FALSE;
		if (!self->pinching &&
		    (fabsf (dist - self->pinch_start_distance) > slop ||
		     distance (self->pinch_start_cx, self->pinch_start_cy, cx, cy) > slop ||
		     a->moved || b->moved))
		{
			LrgTouchGesture *g;

			self->pinching = TRUE;
			self->two_tap_possible = FALSE;
			g = push (self, LRG_TOUCH_GESTURE_PINCH_BEGIN, a->id, cx, cy,
			          self->pinch_start_cx, self->pinch_start_cy, time - self->pair_down_time);
			g->dx = cx - self->pinch_start_cx;
			g->dy = cy - self->pinch_start_cy;
			g->scale = dist / self->pinch_start_distance;
			g->total_scale = g->scale;
			self->pinch_prev_distance = dist;
			self->pinch_prev_cx = cx;
			self->pinch_prev_cy = cy;
		}
		else if (self->pinching &&
		         (dist != self->pinch_prev_distance || cx != self->pinch_prev_cx ||
		          cy != self->pinch_prev_cy))
		{
			LrgTouchGesture *g;

			g = push (self, LRG_TOUCH_GESTURE_PINCH, a->id, cx, cy,
			          self->pinch_start_cx, self->pinch_start_cy, time - self->pair_down_time);
			g->dx = cx - self->pinch_prev_cx;
			g->dy = cy - self->pinch_prev_cy;
			g->scale = dist / self->pinch_prev_distance;
			g->total_scale = dist / self->pinch_start_distance;
			self->pinch_prev_distance = dist;
			self->pinch_prev_cx = cx;
			self->pinch_prev_cy = cy;
		}
	}
	else if (self->pair && self->fingers->len == 0)
	{
		/* both paired fingers gone (a two-finger tap ended the pair) */
		self->pair = FALSE;
	}

	/* 5. tell; a copy, so a handler may feed again */
	if (self->gestures->len == 0)
		return;
	emitted = g_array_copy (self->gestures);
	for (i = 0; i < emitted->len; i++)
		g_signal_emit (self, signals[SIGNAL_GESTURE], 0,
		               &g_array_index (emitted, LrgTouchGesture, i));
}

/* lrg_touch_tracker_update: documented in lrg-touch-tracker.h */
void
lrg_touch_tracker_update (LrgTouchTracker *self,
                          gdouble          time)
{
	LrgTouchPoint points[MAX_DEVICE_POINTS];
	gint count;
	gint i;

	g_return_if_fail (LRG_IS_TOUCH_TRACKER (self));

	count = CLAMP (grl_input_get_touch_point_count (), 0, MAX_DEVICE_POINTS);
	for (i = 0; i < count; i++)
	{
		g_autoptr(GrlVector2) position = grl_input_get_touch_position (i);

		points[i].id = grl_input_get_touch_point_id (i);
		points[i].x = position != NULL ? position->x : 0.0f;
		points[i].y = position != NULL ? position->y : 0.0f;
	}
	lrg_touch_tracker_feed (self, points, (guint) count, time);
}

/* lrg_touch_tracker_reset: documented in lrg-touch-tracker.h */
void
lrg_touch_tracker_reset (LrgTouchTracker *self)
{
	g_return_if_fail (LRG_IS_TOUCH_TRACKER (self));

	g_array_set_size (self->fingers, 0);
	g_array_set_size (self->gestures, 0);
	self->dragging = FALSE;
	self->pair = FALSE;
	self->pinching = FALSE;
	self->two_tap_possible = FALSE;
	self->have_last_tap = FALSE;
}

/* lrg_touch_tracker_get_gestures: documented in lrg-touch-tracker.h */
const LrgTouchGesture *
lrg_touch_tracker_get_gestures (LrgTouchTracker *self,
                                guint           *n_gestures)
{
	g_return_val_if_fail (LRG_IS_TOUCH_TRACKER (self), NULL);
	g_return_val_if_fail (n_gestures != NULL, NULL);

	*n_gestures = self->gestures->len;
	return (const LrgTouchGesture *) self->gestures->data;
}

/* lrg_touch_tracker_has_gesture: documented in lrg-touch-tracker.h */
gboolean
lrg_touch_tracker_has_gesture (LrgTouchTracker     *self,
                               LrgTouchGestureType  type)
{
	guint i;

	g_return_val_if_fail (LRG_IS_TOUCH_TRACKER (self), FALSE);

	for (i = 0; i < self->gestures->len; i++)
		if (g_array_index (self->gestures, LrgTouchGesture, i).type == type)
			return TRUE;
	return FALSE;
}

/* lrg_touch_tracker_get_finger_count: documented in lrg-touch-tracker.h */
guint
lrg_touch_tracker_get_finger_count (LrgTouchTracker *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_TRACKER (self), 0);

	return self->fingers->len;
}

/* lrg_touch_tracker_is_dragging: documented in lrg-touch-tracker.h */
gboolean
lrg_touch_tracker_is_dragging (LrgTouchTracker *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_TRACKER (self), FALSE);

	return self->dragging;
}

/* lrg_touch_tracker_is_pinching: documented in lrg-touch-tracker.h */
gboolean
lrg_touch_tracker_is_pinching (LrgTouchTracker *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_TRACKER (self), FALSE);

	return self->pinching;
}

/* lrg_touch_tracker_get_dp_scale: documented in lrg-touch-tracker.h */
gfloat
lrg_touch_tracker_get_dp_scale (LrgTouchTracker *self)
{
	g_return_val_if_fail (LRG_IS_TOUCH_TRACKER (self), 1.0f);

	return self->dp_scale;
}

/* lrg_touch_tracker_set_dp_scale: documented in lrg-touch-tracker.h */
void
lrg_touch_tracker_set_dp_scale (LrgTouchTracker *self,
                                gfloat           dp_scale)
{
	g_return_if_fail (LRG_IS_TOUCH_TRACKER (self));
	g_return_if_fail (dp_scale > 0.0f);

	if (self->dp_scale == dp_scale)
		return;
	self->dp_scale = dp_scale;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DP_SCALE]);
}
