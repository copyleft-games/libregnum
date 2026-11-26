/* lrg-touch-tracker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Touch gesture recognition: per-frame touch points in, taps, double
 * taps, long presses, drags, pinches and two-finger taps out.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * SECTION:lrg-touch-tracker
 * @title: LrgTouchTracker
 * @short_description: Touch gestures from per-frame touch points
 *
 * A touch screen reports, every frame, the fingers that are down: an id
 * that stays the same while the finger stays down, and a position.  The
 * tracker turns that into gestures:
 *
 * - %LRG_TOUCH_GESTURE_TAP: one finger down and up again quickly
 *   (#LrgTouchTracker:tap-max-duration) without moving
 *   (#LrgTouchTracker:tap-slop);
 * - %LRG_TOUCH_GESTURE_DOUBLE_TAP: a second tap close in time
 *   (#LrgTouchTracker:double-tap-interval) and place
 *   (#LrgTouchTracker:double-tap-slop) to the first; the second tap is
 *   reported as a tap too, so single taps are never delayed;
 * - %LRG_TOUCH_GESTURE_LONG_PRESS: one finger held still for
 *   #LrgTouchTracker:long-press-duration, reported while it is still
 *   down (and its lift is then not a tap);
 * - %LRG_TOUCH_GESTURE_DRAG_BEGIN, %LRG_TOUCH_GESTURE_DRAG and
 *   %LRG_TOUCH_GESTURE_DRAG_END: one finger moving past the tap slop,
 *   with its start and the movement since the last frame;
 * - %LRG_TOUCH_GESTURE_PINCH_BEGIN, %LRG_TOUCH_GESTURE_PINCH and
 *   %LRG_TOUCH_GESTURE_PINCH_END: two fingers moving, with the centre,
 *   its movement and the change in their distance as a scale factor;
 * - %LRG_TOUCH_GESTURE_TWO_FINGER_TAP: two fingers down and up together
 *   quickly without moving.
 *
 * Distances are in dp and multiplied by #LrgTouchTracker:dp-scale
 * (lrg_platform_get_dp_scale()), so the same thresholds feel the same on
 * every screen.
 *
 * Feed it with lrg_touch_tracker_feed() once per frame (synthetic points
 * in tests, the points a game gets from anywhere), or with
 * lrg_touch_tracker_update(), which reads raylib's touch points.  Each
 * gesture is emitted as #LrgTouchTracker::gesture and also kept until
 * the next feed (lrg_touch_tracker_get_gestures()), for polling.
 *
 * Since: 0.13.0
 */

/**
 * LrgTouchPoint:
 * @id: the finger's id, stable while it stays down
 * @x: horizontal position in pixels
 * @y: vertical position in pixels
 *
 * One finger on the screen in one frame.
 *
 * Since: 0.13.0
 */
typedef struct
{
	gint   id;
	gfloat x;
	gfloat y;
} LrgTouchPoint;

#define LRG_TYPE_TOUCH_POINT (lrg_touch_point_get_type ())

LRG_AVAILABLE_IN_ALL
GType          lrg_touch_point_get_type (void) G_GNUC_CONST;

/**
 * lrg_touch_point_copy:
 * @self: an #LrgTouchPoint
 *
 * Returns: (transfer full): a copy of @self
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgTouchPoint *lrg_touch_point_copy     (const LrgTouchPoint *self);

/**
 * lrg_touch_point_free:
 * @self: an #LrgTouchPoint from lrg_touch_point_copy()
 *
 * Frees @self.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void           lrg_touch_point_free     (LrgTouchPoint *self);

/**
 * LrgTouchGestureType:
 * @LRG_TOUCH_GESTURE_TAP: a quick touch without movement
 * @LRG_TOUCH_GESTURE_DOUBLE_TAP: a second tap right after a first, near it
 * @LRG_TOUCH_GESTURE_LONG_PRESS: a finger held still (while still down)
 * @LRG_TOUCH_GESTURE_DRAG_BEGIN: one finger started moving
 * @LRG_TOUCH_GESTURE_DRAG: the dragging finger moved this frame
 * @LRG_TOUCH_GESTURE_DRAG_END: the dragging finger lifted (or a second
 *   finger turned the drag into a pinch)
 * @LRG_TOUCH_GESTURE_PINCH_BEGIN: two fingers started moving
 * @LRG_TOUCH_GESTURE_PINCH: the two fingers moved this frame
 * @LRG_TOUCH_GESTURE_PINCH_END: one of the two fingers lifted
 * @LRG_TOUCH_GESTURE_TWO_FINGER_TAP: two fingers tapped together
 *
 * What an #LrgTouchGesture reports.
 *
 * Since: 0.13.0
 */
typedef enum
{
	LRG_TOUCH_GESTURE_TAP,
	LRG_TOUCH_GESTURE_DOUBLE_TAP,
	LRG_TOUCH_GESTURE_LONG_PRESS,
	LRG_TOUCH_GESTURE_DRAG_BEGIN,
	LRG_TOUCH_GESTURE_DRAG,
	LRG_TOUCH_GESTURE_DRAG_END,
	LRG_TOUCH_GESTURE_PINCH_BEGIN,
	LRG_TOUCH_GESTURE_PINCH,
	LRG_TOUCH_GESTURE_PINCH_END,
	LRG_TOUCH_GESTURE_TWO_FINGER_TAP
} LrgTouchGestureType;

LRG_AVAILABLE_IN_ALL
GType lrg_touch_gesture_type_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_TOUCH_GESTURE_TYPE (lrg_touch_gesture_type_get_type ())

/**
 * LrgTouchGesture:
 * @type: what happened
 * @finger: the finger's id (the first finger of a two-finger gesture)
 * @x: where, in pixels: the finger, or the centre of two
 * @y: where, in pixels
 * @start_x: where the finger (or the centre of two) went down
 * @start_y: where it went down
 * @dx: movement since the last frame, in pixels (drags and pinches)
 * @dy: movement since the last frame, in pixels
 * @scale: pinches: the fingers' distance now over their distance last
 *   frame (above 1 spreading, below 1 pinching); 1 otherwise
 * @total_scale: pinches: their distance now over their distance when the
 *   pinch began; 1 otherwise
 * @duration: seconds since the finger (the first of two) went down
 *
 * One recognised gesture.
 *
 * Since: 0.13.0
 */
typedef struct
{
	LrgTouchGestureType type;
	gint                finger;
	gfloat              x;
	gfloat              y;
	gfloat              start_x;
	gfloat              start_y;
	gfloat              dx;
	gfloat              dy;
	gfloat              scale;
	gfloat              total_scale;
	gdouble             duration;
} LrgTouchGesture;

#define LRG_TYPE_TOUCH_GESTURE (lrg_touch_gesture_get_type ())

LRG_AVAILABLE_IN_ALL
GType            lrg_touch_gesture_get_type (void) G_GNUC_CONST;

/**
 * lrg_touch_gesture_copy:
 * @self: an #LrgTouchGesture
 *
 * Returns: (transfer full): a copy of @self
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgTouchGesture *lrg_touch_gesture_copy     (const LrgTouchGesture *self);

/**
 * lrg_touch_gesture_free:
 * @self: an #LrgTouchGesture from lrg_touch_gesture_copy()
 *
 * Frees @self.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void             lrg_touch_gesture_free     (LrgTouchGesture *self);

#define LRG_TYPE_TOUCH_TRACKER (lrg_touch_tracker_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTouchTracker, lrg_touch_tracker, LRG, TOUCH_TRACKER, GObject)

/**
 * lrg_touch_tracker_new:
 *
 * Creates a tracker with the default thresholds and a dp scale of 1.
 *
 * Returns: (transfer full): a new #LrgTouchTracker
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
LrgTouchTracker *lrg_touch_tracker_new (void);

/**
 * lrg_touch_tracker_feed:
 * @self: an #LrgTouchTracker
 * @points: (array length=n_points) (nullable): the fingers down this frame
 * @n_points: the number of @points
 * @time: the frame's time in seconds (any monotonic clock)
 *
 * Processes one frame: fingers missing from @points have lifted, new ids
 * went down.  Clears the previous frame's gestures, then emits
 * #LrgTouchTracker::gesture for each new one.  Points with an id given
 * twice count once (the first).
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_tracker_feed (LrgTouchTracker     *self,
                             const LrgTouchPoint *points,
                             guint                n_points,
                             gdouble              time);

/**
 * lrg_touch_tracker_update:
 * @self: an #LrgTouchTracker
 * @time: the frame's time in seconds
 *
 * lrg_touch_tracker_feed() with raylib's touch points for this frame
 * (none on a desktop without a touch screen).  Needs an open window.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_tracker_update (LrgTouchTracker *self,
                               gdouble          time);

/**
 * lrg_touch_tracker_reset:
 * @self: an #LrgTouchTracker
 *
 * Forgets every finger and gesture without emitting anything (the app
 * paused, the controls were hidden).  Fingers still down count as new
 * on the next feed.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_tracker_reset (LrgTouchTracker *self);

/**
 * lrg_touch_tracker_get_gestures:
 * @self: an #LrgTouchTracker
 * @n_gestures: (out): the number of gestures
 *
 * The gestures of the last feed, in the order they were emitted.
 *
 * Returns: (array length=n_gestures) (transfer none): the gestures,
 *   valid until the next feed or reset
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
const LrgTouchGesture *lrg_touch_tracker_get_gestures (LrgTouchTracker *self,
                                                       guint           *n_gestures);

/**
 * lrg_touch_tracker_has_gesture:
 * @self: an #LrgTouchTracker
 * @type: a gesture type
 *
 * Returns: %TRUE when the last feed produced a gesture of @type
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_tracker_has_gesture (LrgTouchTracker     *self,
                                        LrgTouchGestureType  type);

/**
 * lrg_touch_tracker_get_finger_count:
 * @self: an #LrgTouchTracker
 *
 * Returns: the number of fingers down after the last feed
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_touch_tracker_get_finger_count (LrgTouchTracker *self);

/**
 * lrg_touch_tracker_is_dragging:
 * @self: an #LrgTouchTracker
 *
 * Returns: %TRUE between %LRG_TOUCH_GESTURE_DRAG_BEGIN and
 *   %LRG_TOUCH_GESTURE_DRAG_END
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_tracker_is_dragging (LrgTouchTracker *self);

/**
 * lrg_touch_tracker_is_pinching:
 * @self: an #LrgTouchTracker
 *
 * Returns: %TRUE between %LRG_TOUCH_GESTURE_PINCH_BEGIN and
 *   %LRG_TOUCH_GESTURE_PINCH_END
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_touch_tracker_is_pinching (LrgTouchTracker *self);

/**
 * lrg_touch_tracker_get_dp_scale:
 * @self: an #LrgTouchTracker
 *
 * Returns: pixels per dp (#LrgTouchTracker:dp-scale)
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_touch_tracker_get_dp_scale (LrgTouchTracker *self);

/**
 * lrg_touch_tracker_set_dp_scale:
 * @self: an #LrgTouchTracker
 * @dp_scale: pixels per dp, greater than 0
 *
 * Sets #LrgTouchTracker:dp-scale.
 *
 * Since: 0.13.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_touch_tracker_set_dp_scale (LrgTouchTracker *self,
                                     gfloat           dp_scale);

G_END_DECLS
