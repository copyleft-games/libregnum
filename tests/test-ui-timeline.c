/* test-ui-timeline.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTimelineView: time <-> x mapping, ruler ticks, clip drag with
 * snapping, resize clamps, overlaps, keyframes and interpolation,
 * scrubbing, marker drag, playback, culling, collapsed layout, editing.
 *
 * Headless: the timeline sits on a canvas, is driven through
 * lrg_canvas_process_input() and stepped with lrg_canvas_update() at
 * 60 Hz. One GPU case renders it under a hidden window and probes
 * pixels; it skips without a display.
 *
 * Fixture geometry (unless a test says otherwise): timeline at (0, 0),
 * 800 x 400, header column 180 wide, ruler 30 high, so the track area
 * starts at x 180, y 30 and is 620 x 370. Zoom 100 units per second,
 * no scroll: x = 180 + 100 t. Clip lanes are 40 high: row 0 is
 * y 30..70, row 1 y 70..110; clips are inset 4 (y 34..66 in row 0).
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

#define CX (180.0f)
#define CY (30.0f)
#define XT(t) (CX + (gfloat) (t) * 100.0f)

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

/* frame: one input frame with the pointer at (@x, @y) */
static void
frame (LrgCanvas         *canvas,
       gfloat             x,
       gfloat             y,
       gboolean           down,
       gboolean           pressed,
       gboolean           released,
       LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	lrg_canvas_input_init (&input);
	input.pointer_x = x;
	input.pointer_y = y;
	input.delta_time = DT;
	input.modifiers = mods;
	if (down)
		input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	if (pressed)
		input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	if (released)
		input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

/*
 * drag: press at (@x0, @y0), move in a few steps to (@x1, @y1), and
 * (with @release) let go there.
 */
static void
drag (LrgCanvas         *canvas,
      gfloat             x0,
      gfloat             y0,
      gfloat             x1,
      gfloat             y1,
      LrgInputModifiers  mods,
      gboolean           release)
{
	guint i;

	frame (canvas, x0, y0, FALSE, FALSE, FALSE, mods);
	frame (canvas, x0, y0, TRUE, TRUE, FALSE, mods);
	for (i = 1; i <= 4; i++)
	{
		gfloat f = (gfloat) i / 4.0f;

		frame (canvas, x0 + (x1 - x0) * f, y0 + (y1 - y0) * f, TRUE, FALSE, FALSE, mods);
	}
	if (release)
		frame (canvas, x1, y1, FALSE, FALSE, TRUE, mods);
}

static void
click_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	frame (canvas, x, y, FALSE, FALSE, FALSE, 0);
	frame (canvas, x, y, TRUE, TRUE, FALSE, 0);
	frame (canvas, x, y, FALSE, FALSE, TRUE, 0);
}

static void
key (LrgCanvas         *canvas,
     GrlKey             k,
     LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	lrg_canvas_input_init (&input);
	input.pointer_x = -10.0f;
	input.pointer_y = -10.0f;
	input.delta_time = DT;
	input.modifiers = mods;
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

static void
wheel (LrgCanvas         *canvas,
       gfloat             x,
       gfloat             y,
       gfloat             amount,
       LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	lrg_canvas_input_init (&input);
	input.pointer_x = x;
	input.pointer_y = y;
	input.delta_time = DT;
	input.modifiers = mods;
	input.wheel_y = amount;
	lrg_canvas_process_input (canvas, &input);
}

/* settle: run frames until nothing animates (at most @max) */
static guint
settle (LrgCanvas *canvas,
        guint      max)
{
	guint n = 0;

	while (n < max && lrg_canvas_update (canvas, DT))
	{
		lrg_canvas_record (canvas);
		n++;
	}
	lrg_canvas_record (canvas);
	g_assert_cmpuint (n, <, max);
	return n;
}

static void
on_count (gpointer user_data)
{
	(*(guint *) user_data)++;
}

static void
on_count_id (LrgTimelineView *tl,
             guint            id,
             gpointer         user_data)
{
	(*(guint *) user_data)++;
}

static void
on_count_key (LrgTimelineView      *tl,
              LrgTimelineViewTrack *track,
              guint                 id,
              gpointer              user_data)
{
	(*(guint *) user_data)++;
}

/*
 * fixture: a timeline with two tracks "A" and "B" on @canvas (which
 * owns it), 20 s long, 40 high lanes, follow-playhead off.
 */
static LrgTimelineView *
fixture (LrgCanvas             *canvas,
         LrgTimelineViewTrack **a,
         LrgTimelineViewTrack **b)
{
	LrgTimelineView *tl = lrg_timeline_view_new ();

	lrg_timeline_view_set_duration (tl, 20.0);
	lrg_timeline_view_set_track_height (tl, 40.0f);
	lrg_timeline_view_set_follow_playhead (tl, FALSE);
	*a = lrg_timeline_view_add_track (tl, "A", NULL);
	*b = lrg_timeline_view_add_track (tl, "B", NULL);

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (tl));
	g_object_unref (tl);
	lrg_widget_allocate (LRG_WIDGET (tl), 0.0f, 0.0f, 800.0f, 400.0f);
	settle (canvas, 120);
	return tl;
}

/* ==========================================================================
 * Mapping and zoom
 * ========================================================================== */

static void
test_mapping (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	gdouble               t0;
	gdouble               t1;

	/* Default: 100 units per second from the track area's left edge */
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_time_to_x (tl, 0.0), CX, 1e-4);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_time_to_x (tl, 2.0), 380.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_x_to_time (tl, 430.0f), 2.5, 1e-6);
	lrg_timeline_view_get_visible_range (tl, &t0, &t1);
	g_assert_cmpfloat_with_epsilon (t0, 0.0, 1e-6);
	g_assert_cmpfloat_with_epsilon (t1, 6.2, 1e-6);

	/* Zoom keeps the left edge time; scroll moves it */
	lrg_timeline_view_set_zoom (tl, 200.0);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_time_to_x (tl, 1.0), 380.0f, 1e-4);
	lrg_timeline_view_set_scroll_time (tl, 1.0, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_scroll_time (tl), 1.0, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_time_to_x (tl, 1.0), CX, 1e-4);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_x_to_time (tl, 380.0f), 2.0, 1e-6);

	/* Zoom about a point pins the time under it */
	lrg_timeline_view_zoom_to (tl, 400.0, 380.0f, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_x_to_time (tl, 380.0f), 2.0, 1e-4);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_zoom (tl), 400.0, 1e-6);

	/* Scroll is clamped to the extent (20 s * 400 + tail - 620) */
	lrg_timeline_view_set_scroll_time (tl, 1000.0, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_scroll_time (tl) * 400.0,
	                                20.0 * 400.0 + 64.0 - 620.0, 1.0);
	lrg_timeline_view_set_scroll_time (tl, -5.0, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_scroll_time (tl), 0.0, 1e-6);
}

/* Ctrl + wheel zooms smoothly about the pointer: the time under the
 * pointer holds on every frame of the glide */
static void
test_wheel_zoom (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	gdouble               before;
	guint                 i;

	lrg_timeline_view_set_scroll_time (tl, 2.0, FALSE);
	before = lrg_timeline_view_x_to_time (tl, 480.0f);
	wheel (canvas, 480.0f, 50.0f, 1.0f, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_zoom (tl), 120.0, 1e-3);

	for (i = 0; i < 4; i++)
	{
		lrg_canvas_update (canvas, DT);
		g_assert_cmpfloat_with_epsilon (lrg_timeline_view_x_to_time (tl, 480.0f), before, 0.02);
	}
	settle (canvas, 240);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_time_to_x (tl, 1.0) - lrg_timeline_view_time_to_x (tl, 0.0),
	                                120.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_x_to_time (tl, 480.0f), before, 0.02);

	/* Shift + wheel scrolls horizontally; a plain wheel too when the
	 * tracks fit vertically */
	before = lrg_timeline_view_get_scroll_time (tl);
	wheel (canvas, 480.0f, 50.0f, -1.0f, LRG_INPUT_MODIFIER_SHIFT);
	settle (canvas, 240);
	g_assert_cmpfloat (lrg_timeline_view_get_scroll_time (tl), >, before);
}

/* ==========================================================================
 * Ruler
 * ========================================================================== */

static void
test_ruler_ticks (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	gdouble               major;
	guint                 minor;
	gchar                *s;

	/* 100 u/s: one second, fifths */
	lrg_timeline_view_get_tick_step (tl, &major, &minor);
	g_assert_cmpfloat_with_epsilon (major, 1.0, 1e-9);
	g_assert_cmpuint (minor, ==, 5);

	/* 1000 u/s: tenths */
	lrg_timeline_view_set_zoom (tl, 1000.0);
	lrg_timeline_view_get_tick_step (tl, &major, &minor);
	g_assert_cmpfloat_with_epsilon (major, 0.1, 1e-9);
	g_assert_cmpuint (minor, ==, 5);

	/* 2 u/s: minutes, in sixths */
	lrg_timeline_view_set_zoom (tl, 2.0);
	lrg_timeline_view_get_tick_step (tl, &major, &minor);
	g_assert_cmpfloat_with_epsilon (major, 60.0, 1e-9);
	g_assert_cmpuint (minor, ==, 6);

	/* 30 fps: frames when zoomed in, seconds in 15ths at 100 u/s */
	lrg_timeline_view_set_fps (tl, 30.0);
	lrg_timeline_view_set_zoom (tl, 1000.0);
	lrg_timeline_view_get_tick_step (tl, &major, &minor);
	g_assert_cmpfloat_with_epsilon (major, 5.0 / 30.0, 1e-9);
	g_assert_cmpuint (minor, ==, 5);
	lrg_timeline_view_set_zoom (tl, 100.0);
	lrg_timeline_view_get_tick_step (tl, &major, &minor);
	g_assert_cmpfloat_with_epsilon (major, 1.0, 1e-9);
	g_assert_cmpuint (minor, ==, 15);

	/* Labels in each format */
	lrg_timeline_view_set_fps (tl, 0.0);
	s = lrg_timeline_view_format_time (tl, 65.0);
	g_assert_cmpstr (s, ==, "1:05");
	g_free (s);
	lrg_timeline_view_set_zoom (tl, 1000.0);
	s = lrg_timeline_view_format_time (tl, 1.5);
	g_assert_cmpstr (s, ==, "1.5s");
	g_free (s);
	lrg_timeline_view_set_fps (tl, 30.0);
	lrg_timeline_view_set_time_format (tl, LRG_TIMELINE_VIEW_TIME_FORMAT_TIMECODE);
	s = lrg_timeline_view_format_time (tl, 61.5);
	g_assert_cmpstr (s, ==, "01:01:15");
	g_free (s);
	lrg_timeline_view_set_time_format (tl, LRG_TIMELINE_VIEW_TIME_FORMAT_FRAMES);
	s = lrg_timeline_view_format_time (tl, 2.0);
	g_assert_cmpstr (s, ==, "60");
	g_free (s);
}

/* ==========================================================================
 * Clips
 * ========================================================================== */

static void
test_overlap_rule (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	guint                 c1;
	guint                 c2;
	guint                 c3;

	c1 = lrg_timeline_view_add_clip (tl, a, 1.0, 2.0, "one");
	g_assert_cmpuint (c1, !=, LRG_TIMELINE_VIEW_INVALID_ID);

	/* Overlapping is refused, touching is fine */
	g_assert_cmpuint (lrg_timeline_view_add_clip (tl, a, 2.0, 2.0, NULL), ==, LRG_TIMELINE_VIEW_INVALID_ID);
	c2 = lrg_timeline_view_add_clip (tl, a, 3.0, 1.0, "two");
	g_assert_cmpuint (c2, !=, LRG_TIMELINE_VIEW_INVALID_ID);
	g_assert_cmpuint (lrg_timeline_view_get_n_clips (tl, a), ==, 2);

	/* Moving into another clip fails and changes nothing */
	g_assert_false (lrg_timeline_view_move_clip (tl, c2, NULL, 2.5));
	g_assert_cmpfloat (lrg_timeline_view_get_clip_start (tl, c2), ==, 3.0);
	g_assert_true (lrg_timeline_view_move_clip (tl, c2, b, 2.5));
	g_assert_true (lrg_timeline_view_get_clip_track (tl, c2) == b);

	/* Tracks that allow overlaps take anything */
	lrg_timeline_view_track_set_allow_overlap (a, TRUE);
	c3 = lrg_timeline_view_add_clip (tl, a, 2.0, 2.0, "three");
	g_assert_cmpuint (c3, !=, LRG_TIMELINE_VIEW_INVALID_ID);
	g_assert_cmpuint (lrg_timeline_view_get_clip_at (tl, a, 2.5), ==, c3);
	g_assert_cmpuint (lrg_timeline_view_get_clip_at (tl, a, 1.5), ==, c1);
	g_assert_cmpuint (lrg_timeline_view_get_clip_at (tl, a, 5.0), ==, LRG_TIMELINE_VIEW_INVALID_ID);

	g_assert_true (lrg_timeline_view_remove_clip (tl, c3));
	g_assert_false (lrg_timeline_view_remove_clip (tl, c3));
	g_assert_cmpuint (lrg_timeline_view_get_n_clips (tl, NULL), ==, 2);
}

/* Dragging a clip: its end snaps onto another clip's start (with the
 * guide drawn), Alt drags freely, and a single clip changes track */
static void
test_clip_drag_snap (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	guint                 moved = 0;
	guint                 selected = 0;
	guint                 c1;
	guint                 c2;
	guint                 n_before;
	guint                 n_during;

	c1 = lrg_timeline_view_add_clip (tl, a, 1.0, 2.0, "walk");
	c2 = lrg_timeline_view_add_clip (tl, b, 5.0, 1.0, "talk");
	g_signal_connect (tl, "clip-moved", G_CALLBACK (on_count_id), &moved);
	g_signal_connect (tl, "clip-selected", G_CALLBACK (on_count_id), &selected);
	n_before = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));

	/* Grab the middle (t 2) and move by 1.96 s: the end (4.96) lands
	 * within 8 units of c2's start (5) and snaps there */
	drag (canvas, XT (2.0), 50.0f, XT (3.96), 50.0f, 0, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_start (tl, c1), 3.0, 1e-9);
	g_assert_true (lrg_timeline_view_is_clip_selected (tl, c1));
	g_assert_cmpuint (selected, ==, 1);

	/* Mid-drag the guide line is drawn: more instances than at rest */
	n_during = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
	g_assert_cmpuint (n_during, >, n_before);
	frame (canvas, XT (3.96), 50.0f, FALSE, FALSE, TRUE, 0);
	g_assert_cmpuint (moved, ==, 1);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_duration (tl, c1), 2.0, 1e-9);

	/* Alt: no snapping, no grid */
	drag (canvas, XT (4.0), 50.0f, XT (4.0) - 33.0f, 50.0f, LRG_INPUT_MODIFIER_ALT, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_start (tl, c1), 2.67, 1e-4);
	g_assert_cmpuint (moved, ==, 2);

	/* Without a magnet the grid (0.2 s minor ticks) quantizes the start */
	drag (canvas, XT (3.5), 50.0f, XT (3.5) + 51.0f, 50.0f, 0, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_start (tl, c1), 3.2, 1e-9);

	/* Down onto track B where it fits (c2 is at 5..6, the clip at 3.2..5.2
	 * would overlap; drag left first to make room) */
	drag (canvas, XT (4.0), 50.0f, XT (1.0), 90.0f, LRG_INPUT_MODIFIER_ALT, TRUE);
	g_assert_true (lrg_timeline_view_get_clip_track (tl, c1) == b);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_start (tl, c1), 0.2, 1e-4);

	/* Into c2 on its own track: refused, the clip stays put */
	drag (canvas, XT (1.0), 90.0f, XT (4.5), 90.0f, LRG_INPUT_MODIFIER_ALT, TRUE);
	g_assert_cmpfloat (lrg_timeline_view_get_clip_start (tl, c1) + 2.0, <=, 5.0 + 1e-9);
	g_assert_cmpfloat (lrg_timeline_view_get_clip_start (tl, c2), ==, 5.0);
}

static void
test_resize_clamps (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	guint                 resized = 0;
	guint                 c1;
	guint                 c2;

	c1 = lrg_timeline_view_add_clip (tl, a, 1.0, 2.0, "c1");
	c2 = lrg_timeline_view_add_clip (tl, a, 4.0, 2.0, "c2");
	g_signal_connect (tl, "clip-resized", G_CALLBACK (on_count_id), &resized);

	/* API: the end stops at the neighbour, the start at 0, the length
	 * at the minimum (10 ms without an fps) */
	g_assert_true (lrg_timeline_view_resize_clip (tl, c1, 1.0, 10.0));
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_duration (tl, c1), 3.0, 1e-9);
	g_assert_true (lrg_timeline_view_resize_clip (tl, c1, -1.0, 0.0));
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_start (tl, c1), 0.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_duration (tl, c1), 0.01, 1e-9);
	g_assert_true (lrg_timeline_view_resize_clip (tl, c2, 4.0, 1.0));
	g_assert_true (lrg_timeline_view_resize_clip (tl, c1, 1.0, 2.0));
	g_assert_cmpuint (resized, ==, 4);

	/* Drag the right edge (x 480) far right: stops at c2 (t 4) */
	drag (canvas, XT (3.0) - 2.0f, 50.0f, XT (5.5), 50.0f, 0, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_start (tl, c1), 1.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_duration (tl, c1), 3.0, 1e-9);
	g_assert_cmpuint (resized, ==, 5);

	/* Drag the left edge past the right one: the minimum length remains */
	drag (canvas, XT (1.0) + 2.0f, 50.0f, XT (6.0), 50.0f, LRG_INPUT_MODIFIER_ALT, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_start (tl, c1), 3.99, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_duration (tl, c1), 0.01, 1e-6);

	/* Left edge back before 0: clamps at 0 */
	lrg_timeline_view_resize_clip (tl, c1, 1.0, 2.0);
	drag (canvas, XT (1.0) + 2.0f, 50.0f, CX - 60.0f, 50.0f, LRG_INPUT_MODIFIER_ALT, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_start (tl, c1), 0.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_duration (tl, c1), 3.0, 1e-9);

	/* Locked tracks refuse pointer edits */
	lrg_timeline_view_track_set_locked (a, TRUE);
	drag (canvas, XT (3.0) - 2.0f, 50.0f, XT (3.5), 50.0f, 0, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_clip_duration (tl, c1), 3.0, 1e-9);
}

/* ==========================================================================
 * Keyframes
 * ========================================================================== */

static void
test_keyframes (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	guint                 moved = 0;
	guint                 added = 0;
	guint                 k1;
	guint                 k2;
	guint                 k3;
	gfloat                ry;
	gfloat                rh;

	/* Interpolation evaluation */
	g_assert_cmpfloat (lrg_timeline_view_evaluate (tl, a, 1.0), ==, 0.0);
	lrg_timeline_view_track_set_default_value (a, 0.25);
	g_assert_cmpfloat (lrg_timeline_view_evaluate (tl, a, 1.0), ==, 0.25);

	lrg_timeline_view_add_keyframe (tl, a, 0.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	k1 = lrg_timeline_view_add_keyframe (tl, a, 2.0, 1.0, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	k2 = lrg_timeline_view_add_keyframe (tl, a, 4.0, 0.0, LRG_TIMELINE_VIEW_INTERPOLATION_STEP);
	k3 = lrg_timeline_view_add_keyframe (tl, a, 6.0, 0.5, LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER);
	lrg_timeline_view_add_keyframe (tl, a, 8.0, 0.5, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	g_assert_cmpuint (lrg_timeline_view_get_n_keyframes (tl, a), ==, 5);
	g_assert_cmpuint (lrg_timeline_view_get_keyframe (tl, a, 1), ==, k1);

	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_evaluate (tl, a, -1.0), 0.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_evaluate (tl, a, 1.0), 0.5, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_evaluate (tl, a, 3.0), 0.5, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_evaluate (tl, a, 2.5), 0.84375, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_evaluate (tl, a, 5.0), 0.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_evaluate (tl, a, 7.0), 0.5, 1e-9);
	lrg_timeline_view_set_keyframe_tangents (tl, k3, 0.0, 1.0);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_evaluate (tl, a, 7.0), 0.75, 1e-9);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_evaluate (tl, a, 10.0), 0.5, 1e-9);

	/* Show the curve lane (80 high, under row A's clip lane: y 70..150) */
	lrg_timeline_view_track_set_curve_lane (a, TRUE);
	lrg_timeline_view_track_set_curve_height (a, 80.0f);
	g_assert_true (lrg_timeline_view_get_track_row (tl, a, &ry, &rh));
	g_assert_cmpfloat (ry, ==, CY);
	g_assert_cmpfloat (rh, ==, 120.0f);
	settle (canvas, 120);
	g_signal_connect (tl, "keyframe-moved", G_CALLBACK (on_count_key), &moved);
	g_signal_connect (tl, "keyframe-added", G_CALLBACK (on_count_key), &added);

	/* Drag k1's diamond (t 2, y 50) to t ~3: the grid snaps it to 3.0 */
	drag (canvas, XT (2.0), 50.0f, XT (3.02), 50.0f, 0, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_keyframe_time (tl, k1), 3.0, 1e-9);
	g_assert_cmpfloat (lrg_timeline_view_get_keyframe_value (tl, k1), ==, 1.0);
	g_assert_cmpuint (moved, ==, 1);
	g_assert_true (lrg_timeline_view_is_keyframe_selected (tl, k1));

	/* Drag k2's curve point (t 4, value 0 -> y 70 + 8 + 64 = 142) up to
	 * the middle (y 110 -> value 0.5) */
	drag (canvas, XT (4.0), 142.0f, XT (4.0), 110.0f, 0, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_keyframe_value (tl, k2), 0.5, 1e-4);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_keyframe_time (tl, k2), 4.0, 1e-9);
	g_assert_cmpuint (moved, ==, 2);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_evaluate (tl, a, 4.5), 0.5, 1e-4);

	/* Double click in the curve lane adds a keyframe there */
	click_at (canvas, XT (5.0), 110.0f);
	click_at (canvas, XT (5.0), 110.0f);
	g_assert_cmpuint (added, ==, 1);
	g_assert_cmpuint (lrg_timeline_view_get_n_keyframes (tl, a), ==, 6);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_keyframe_value (tl, lrg_timeline_view_get_keyframe (tl, a, 3)),
	                                0.5, 1e-4);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_keyframe_time (tl, lrg_timeline_view_get_keyframe (tl, a, 3)),
	                                5.0, 1e-9);

	/* Delete removes the selected (new) keyframe */
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (tl));
	key (canvas, GRL_KEY_DELETE, 0);
	g_assert_cmpuint (lrg_timeline_view_get_n_keyframes (tl, a), ==, 5);
}

/* ==========================================================================
 * Playhead and playback
 * ========================================================================== */

static void
test_scrub (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	guint                 moved = 0;
	guint                 activated = 0;
	guint                 m;

	g_signal_connect_swapped (tl, "playhead-moved", G_CALLBACK (on_count), &moved);
	g_signal_connect (tl, "marker-activated", G_CALLBACK (on_count_id), &activated);

	/* Press on the ruler: the playhead jumps there, then follows the
	 * pointer (grid: 0.2 s) */
	drag (canvas, XT (2.0), 15.0f, XT (2.51), 15.0f, 0, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 2.6, 1e-9);
	g_assert_cmpuint (moved, >=, 2);
	frame (canvas, XT (2.51), 15.0f, FALSE, FALSE, TRUE, 0);

	/* Alt scrubs freely; past the end clamps to the duration */
	drag (canvas, XT (1.0), 15.0f, XT (1.37), 15.0f, LRG_INPUT_MODIFIER_ALT, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 1.37, 1e-4);

	/* Scrubbing snaps to clip edges */
	lrg_timeline_view_add_clip (tl, a, 3.33, 1.0, NULL);
	drag (canvas, XT (1.0), 15.0f, XT (3.30), 15.0f, 0, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 3.33, 1e-9);

	/* Clicking a marker flag (lower half of the ruler) activates it */
	m = lrg_timeline_view_add_marker (tl, 5.0, "Boom");
	click_at (canvas, XT (5.0) + 5.0f, 25.0f);
	g_assert_cmpuint (activated, ==, 1);
	g_assert_cmpfloat (lrg_timeline_view_get_time (tl), ==, 5.0);
	g_assert_cmpstr (lrg_timeline_view_get_marker_label (tl, m), ==, "Boom");

	/* Keys: Right steps 0.1 s without an fps, a frame with one */
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (tl));
	key (canvas, GRL_KEY_RIGHT, 0);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 5.1, 1e-9);
	lrg_timeline_view_set_fps (tl, 24.0);
	key (canvas, GRL_KEY_LEFT, 0);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 121.0 / 24.0, 1e-9);
	key (canvas, GRL_KEY_HOME, 0);
	g_assert_cmpfloat (lrg_timeline_view_get_time (tl), ==, 0.0);
}

/* ==========================================================================
 * Marker drag
 * ========================================================================== */

/* What the last marker-moved emission carried */
typedef struct
{
	guint   count;
	guint   marker;
	gdouble old_time;
	gdouble new_time;
} MarkerMoved;

static void
on_marker_moved (LrgTimelineView *tl,
                 guint            marker,
                 gdouble          old_time,
                 gdouble          new_time,
                 gpointer         user_data)
{
	MarkerMoved *mm = user_data;

	mm->count++;
	mm->marker = marker;
	mm->old_time = old_time;
	mm->new_time = new_time;
}

/*
 * The flag of a marker is the lower half of the ruler (y 15..30), from
 * 3 units left of the marker to past its label; presses there grab the
 * marker at y 25, a few units right of the marker line.
 */
#define FLAG_Y (25.0f)
#define FLAG_X(t) (XT (t) + 5.0f)

static void
test_marker_drag (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	MarkerMoved           mm = { 0, 0, 0.0, 0.0 };
	guint                 activated = 0;
	guint                 m;
	guint                 other;

	g_signal_connect (tl, "marker-moved", G_CALLBACK (on_marker_moved), &mm);
	g_signal_connect (tl, "marker-activated", G_CALLBACK (on_count_id), &activated);
	m = lrg_timeline_view_add_marker (tl, 1.0, "A");

	/* Dragging the flag moves the marker live; one signal at release
	 * with the old and new time; no activation, the playhead stays */
	drag (canvas, FLAG_X (1.0), FLAG_Y, FLAG_X (2.0), FLAG_Y, 0, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_marker_time (tl, m), 2.0, 1e-9);
	g_assert_cmpuint (mm.count, ==, 0);
	frame (canvas, FLAG_X (2.0), FLAG_Y, FALSE, FALSE, TRUE, 0);
	g_assert_cmpuint (mm.count, ==, 1);
	g_assert_cmpuint (mm.marker, ==, m);
	g_assert_cmpfloat_with_epsilon (mm.old_time, 1.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (mm.new_time, 2.0, 1e-9);
	g_assert_cmpuint (activated, ==, 0);
	g_assert_cmpfloat (lrg_timeline_view_get_time (tl), ==, 0.0);

	/* Grid: without a magnet in reach the time rounds to the minor
	 * tick (0.2 s at this zoom); Alt drags freely */
	drag (canvas, FLAG_X (2.0), FLAG_Y, FLAG_X (2.53), FLAG_Y, 0, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_marker_time (tl, m), 2.6, 1e-9);
	drag (canvas, FLAG_X (2.6), FLAG_Y, FLAG_X (2.97), FLAG_Y, LRG_INPUT_MODIFIER_ALT, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_marker_time (tl, m), 2.97, 1e-4);
	g_assert_cmpuint (mm.count, ==, 3);

	/* With fps the grid is a frame */
	lrg_timeline_view_set_fps (tl, 24.0);
	lrg_timeline_view_move_marker (tl, m, 2.0);
	drag (canvas, FLAG_X (2.0), FLAG_Y, FLAG_X (2.5), FLAG_Y, 0, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_marker_time (tl, m), 12.0 / 24.0 + 2.0, 1e-9);
	lrg_timeline_view_set_fps (tl, 0.0);
	g_assert_cmpuint (mm.count, ==, 4);

	/* Snaps to another marker and to the playhead (not to itself) */
	other = lrg_timeline_view_add_marker (tl, 5.0, NULL);
	lrg_timeline_view_move_marker (tl, m, 3.0);
	drag (canvas, FLAG_X (3.0), FLAG_Y, FLAG_X (4.95), FLAG_Y, 0, TRUE);
	g_assert_cmpfloat (lrg_timeline_view_get_marker_time (tl, m), ==, 5.0);
	g_assert_cmpfloat (lrg_timeline_view_get_marker_time (tl, other), ==, 5.0);
	lrg_timeline_view_remove_marker (tl, other);

	lrg_timeline_view_set_time (tl, 3.33);
	lrg_timeline_view_move_marker (tl, m, 2.0);
	drag (canvas, FLAG_X (2.0), FLAG_Y, FLAG_X (3.30), FLAG_Y, 0, TRUE);
	g_assert_cmpfloat (lrg_timeline_view_get_marker_time (tl, m), ==, 3.33);
	g_assert_cmpfloat_with_epsilon (mm.new_time, 3.33, 1e-12);
	g_assert_cmpfloat_with_epsilon (mm.old_time, 2.0, 1e-12);
	g_assert_cmpuint (mm.count, ==, 6);

	/* A drag that ends where it started emits nothing */
	drag (canvas, FLAG_X (3.33), FLAG_Y, FLAG_X (3.33) + 30.0f, FLAG_Y, 0, FALSE);
	frame (canvas, FLAG_X (3.33), FLAG_Y, TRUE, FALSE, FALSE, 0);
	frame (canvas, FLAG_X (3.33), FLAG_Y, FALSE, FALSE, TRUE, 0);
	g_assert_cmpfloat (lrg_timeline_view_get_marker_time (tl, m), ==, 3.33);
	g_assert_cmpuint (mm.count, ==, 6);

	/* Esc mid-drag puts the marker back, with no signal, and the
	 * release that follows neither moves nor activates it */
	drag (canvas, FLAG_X (3.33), FLAG_Y, FLAG_X (5.0), FLAG_Y, 0, FALSE);
	g_assert_cmpfloat (lrg_timeline_view_get_marker_time (tl, m), !=, 3.33);
	key (canvas, GRL_KEY_ESCAPE, 0);
	g_assert_cmpfloat (lrg_timeline_view_get_marker_time (tl, m), ==, 3.33);
	frame (canvas, FLAG_X (5.0), FLAG_Y, FALSE, FALSE, TRUE, 0);
	g_assert_cmpfloat (lrg_timeline_view_get_marker_time (tl, m), ==, 3.33);
	g_assert_cmpuint (mm.count, ==, 6);
	g_assert_cmpuint (activated, ==, 0);

	/* A click on the flag (no drag) still activates the marker */
	click_at (canvas, FLAG_X (3.33), FLAG_Y);
	g_assert_cmpuint (activated, ==, 1);
	g_assert_cmpuint (mm.count, ==, 6);

	/* The ruler away from a flag, and above the flags, still scrubs */
	drag (canvas, XT (5.0), FLAG_Y, XT (5.4), FLAG_Y, 0, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 5.4, 1e-9);
	drag (canvas, FLAG_X (3.33), 5.0f, XT (1.0), 5.0f, 0, TRUE);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 1.0, 1e-9);
	g_assert_cmpfloat (lrg_timeline_view_get_marker_time (tl, m), ==, 3.33);
	g_assert_cmpuint (mm.count, ==, 6);

	/* Removing the marker mid-drag ends the drag quietly */
	drag (canvas, FLAG_X (3.33), FLAG_Y, FLAG_X (4.0), FLAG_Y, 0, FALSE);
	g_assert_true (lrg_timeline_view_remove_marker (tl, m));
	frame (canvas, FLAG_X (4.0), FLAG_Y, FALSE, FALSE, TRUE, 0);
	g_assert_cmpuint (mm.count, ==, 6);
	g_assert_cmpuint (lrg_timeline_view_get_n_markers (tl), ==, 0);
}

static void
run_frames (LrgCanvas *canvas,
            guint      n)
{
	guint i;

	for (i = 0; i < n; i++)
		lrg_canvas_update (canvas, DT);
}

static void
test_playback (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);

	lrg_timeline_view_set_duration (tl, 4.0);

	/* One second of frames advances one second */
	lrg_timeline_view_set_playing (tl, TRUE);
	run_frames (canvas, 60);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 1.0, 1e-4);

	/* Half speed */
	lrg_timeline_view_set_rate (tl, 0.5);
	run_frames (canvas, 60);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 1.5, 1e-4);
	lrg_timeline_view_set_rate (tl, 1.0);

	/* Loop range 1..2 wraps */
	lrg_timeline_view_set_loop (tl, TRUE);
	lrg_timeline_view_set_loop_range (tl, 1.0, 2.0);
	lrg_timeline_view_set_time (tl, 1.9);
	run_frames (canvas, 12);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 1.1, 1e-4);
	g_assert_true (lrg_timeline_view_get_playing (tl));

	/* Backwards over the whole sequence (empty range) wraps at 0 */
	lrg_timeline_view_set_loop_range (tl, 0.0, 0.0);
	lrg_timeline_view_set_rate (tl, -1.0);
	lrg_timeline_view_set_time (tl, 0.1);
	run_frames (canvas, 12);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_time (tl), 3.9, 1e-4);

	/* No loop: stops at the end */
	lrg_timeline_view_set_loop (tl, FALSE);
	lrg_timeline_view_set_rate (tl, 1.0);
	lrg_timeline_view_set_time (tl, 3.9);
	run_frames (canvas, 12);
	g_assert_cmpfloat (lrg_timeline_view_get_time (tl), ==, 4.0);
	g_assert_false (lrg_timeline_view_get_playing (tl));

	/* Play button and Enter toggle playback (restarting from the top) */
	click_at (canvas, 8.0f + 11.0f, 15.0f);
	g_assert_true (lrg_timeline_view_get_playing (tl));
	g_assert_cmpfloat (lrg_timeline_view_get_time (tl), <, 0.1);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (tl));
	key (canvas, GRL_KEY_ENTER, 0);
	g_assert_false (lrg_timeline_view_get_playing (tl));

	/* Follow playhead: jumping out of view pages the view after it */
	lrg_timeline_view_set_duration (tl, 20.0);
	lrg_timeline_view_set_follow_playhead (tl, TRUE);
	lrg_timeline_view_set_time (tl, 10.0);
	settle (canvas, 240);
	g_assert_cmpfloat_with_epsilon (lrg_timeline_view_get_scroll_time (tl), 9.38, 0.011);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
test_collapsed_layout (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	LrgTimelineViewTrack *g;
	LrgTimelineViewTrack *g1;
	LrgTimelineViewTrack *g2;
	LrgTimelineViewTrack *g3;
	gfloat                y;
	gfloat                h;
	gfloat                ch;

	/* A (with a 60 curve lane), B, then a group G with G1, G2 */
	lrg_timeline_view_track_set_curve_lane (a, TRUE);
	lrg_timeline_view_track_set_curve_height (a, 60.0f);
	g = lrg_timeline_view_add_track (tl, "G", NULL);
	g1 = lrg_timeline_view_add_track (tl, "G1", g);
	g2 = lrg_timeline_view_add_track (tl, "G2", g);
	g3 = lrg_timeline_view_add_track (tl, "G3", g);
	lrg_timeline_view_add_track (tl, "C", NULL);
	g_assert_true (lrg_timeline_view_track_get_parent (g1) == g);

	/* Nested tracks follow their group, before C */
	g_assert_true (lrg_timeline_view_get_track (tl, 5) == g3);
	g_assert_cmpstr (lrg_timeline_view_track_get_name (lrg_timeline_view_get_track (tl, 6)), ==, "C");

	g_assert_true (lrg_timeline_view_get_track_row (tl, a, &y, &h));
	g_assert_cmpfloat (y, ==, CY);
	g_assert_cmpfloat (h, ==, 100.0f);
	g_assert_true (lrg_timeline_view_get_track_row (tl, b, &y, &h));
	g_assert_cmpfloat (y, ==, CY + 100.0f);
	g_assert_true (lrg_timeline_view_get_track_row (tl, g2, &y, NULL));
	g_assert_cmpfloat (y, ==, CY + 100.0f + 40.0f * 3.0f);

	/* Collapsing the group hides its tracks and shrinks it */
	lrg_timeline_view_track_set_collapsed (g, TRUE);
	g_assert_true (lrg_timeline_view_get_track_row (tl, g, &y, &ch));
	g_assert_cmpfloat (y, ==, CY + 140.0f);
	g_assert_cmpfloat (ch, <, 40.0f);
	g_assert_false (lrg_timeline_view_get_track_row (tl, g1, NULL, NULL));
	g_assert_false (lrg_timeline_view_get_track_row (tl, g3, NULL, NULL));
	g_assert_true (lrg_timeline_view_get_track_row (tl, lrg_timeline_view_find_track (tl, "C"), &y, NULL));
	g_assert_cmpfloat (y, ==, CY + 140.0f + ch);

	/* A collapsed track drops its curve lane */
	lrg_timeline_view_track_set_collapsed (a, TRUE);
	g_assert_true (lrg_timeline_view_get_track_row (tl, a, NULL, &h));
	g_assert_cmpfloat (h, ==, ch);

	/* Clicking G's chevron (depth 0: x 9..25, row middle) expands it */
	settle (canvas, 120);
	g_assert_true (lrg_timeline_view_get_track_row (tl, g, &y, &h));
	click_at (canvas, 17.0f, y + h * 0.5f);
	g_assert_false (lrg_timeline_view_track_get_collapsed (g));
	g_assert_true (lrg_timeline_view_get_track_row (tl, g1, NULL, NULL));

	/* Mute and lock toggles on the right of the header */
	g_assert_true (lrg_timeline_view_get_track_row (tl, b, &y, &h));
	click_at (canvas, 180.0f - 6.0f - 8.0f, y + 20.0f);
	g_assert_true (lrg_timeline_view_track_get_locked (b));
	click_at (canvas, 180.0f - 6.0f - 16.0f - 4.0f - 8.0f, y + 20.0f);
	g_assert_true (lrg_timeline_view_track_get_muted (b));

	/* Removing the group removes its nested tracks */
	lrg_timeline_view_remove_track (tl, g);
	g_assert_cmpuint (lrg_timeline_view_get_n_tracks (tl), ==, 3);
	g_assert_null (lrg_timeline_view_find_track (tl, "G2"));
}

/* 100 tracks x 1000 clips: only what is on screen is recorded */
static void
test_culling (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	LrgTimelineViewTrack *first = NULL;
	guint                 n_start;
	guint                 n_mid;
	guint                 i;
	guint                 j;

	lrg_timeline_view_remove_track (tl, a);
	lrg_timeline_view_remove_track (tl, b);
	for (i = 0; i < 100; i++)
	{
		g_autofree gchar     *name = g_strdup_printf ("T%u", i);
		LrgTimelineViewTrack *t = lrg_timeline_view_add_track (tl, name, NULL);

		if (first == NULL)
			first = t;
		for (j = 0; j < 1000; j++)
			lrg_timeline_view_add_clip (tl, t, (gdouble) j, 0.5, "clip");
		if (i % 10 == 0)
			lrg_timeline_view_add_keyframe (tl, t, 1.0, 0.5, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	}
	g_assert_cmpuint (lrg_timeline_view_get_n_clips (tl, NULL), ==, 100000);
	lrg_timeline_view_set_duration (tl, 1000.0);
	settle (canvas, 120);

	/* ~9 rows x ~7 clips on screen */
	n_start = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
	g_assert_cmpuint (n_start, <, 4000);

	/* Deep in the middle: about the same */
	lrg_timeline_view_set_scroll_time (tl, 500.0, FALSE);
	lrg_timeline_view_set_scroll_y (tl, 2000.0f, FALSE);
	n_mid = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
	g_assert_cmpuint (n_mid, <, 4000);
	g_assert_cmpuint (n_mid, <, n_start * 2);
	g_assert_cmpuint (n_mid * 2, >, n_start);

	/* Zoomed far out the clips get tiny but stay bounded by the view */
	lrg_timeline_view_set_zoom (tl, 0.5);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (canvas)), <, 4000);

	/* Hit testing stays exact: the first track's clip 3 under the pointer */
	lrg_timeline_view_set_zoom (tl, 100.0);
	lrg_timeline_view_set_scroll_time (tl, 0.0, FALSE);
	lrg_timeline_view_set_scroll_y (tl, 0.0f, FALSE);
	click_at (canvas, XT (3.25), 50.0f);
	g_assert_true (lrg_timeline_view_is_clip_selected (tl, lrg_timeline_view_get_clip_at (tl, first, 3.25)));
}

/* ==========================================================================
 * Editing: box select, delete, copy / paste
 * ========================================================================== */

static void
test_editing (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl = fixture (canvas, &a, &b);
	g_autoptr(GArray)     sel = NULL;
	guint                 changed = 0;
	guint                 removed = 0;
	guint                 c1;
	guint                 c2;
	guint                 c3;

	c1 = lrg_timeline_view_add_clip (tl, a, 1.0, 1.0, "c1");
	c2 = lrg_timeline_view_add_clip (tl, b, 3.0, 1.0, "c2");
	c3 = lrg_timeline_view_add_clip (tl, b, 5.0, 1.0, "c3");
	g_signal_connect_swapped (tl, "selection-changed", G_CALLBACK (on_count), &changed);
	g_signal_connect (tl, "clip-removed", G_CALLBACK (on_count_id), &removed);

	/* Rubber band from (t 0.5, row A) to (t 3.5, row B) */
	drag (canvas, XT (0.5), 40.0f, XT (3.5), 100.0f, 0, TRUE);
	sel = lrg_timeline_view_get_selected_clips (tl);
	g_assert_cmpuint (sel->len, ==, 2);
	g_assert_cmpuint (g_array_index (sel, guint, 0), ==, c1);
	g_assert_cmpuint (g_array_index (sel, guint, 1), ==, c2);
	g_assert_cmpuint (changed, ==, 1);

	/* Ctrl + click adds c3, then Delete removes all three */
	frame (canvas, XT (5.5), 90.0f, FALSE, FALSE, FALSE, LRG_INPUT_MODIFIER_CTRL);
	frame (canvas, XT (5.5), 90.0f, TRUE, TRUE, FALSE, LRG_INPUT_MODIFIER_CTRL);
	frame (canvas, XT (5.5), 90.0f, FALSE, FALSE, TRUE, LRG_INPUT_MODIFIER_CTRL);
	g_assert_true (lrg_timeline_view_is_clip_selected (tl, c3));
	g_assert_true (lrg_timeline_view_is_clip_selected (tl, c1));

	/* Copy them, delete them, paste at 10 s */
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (tl));
	key (canvas, GRL_KEY_C, LRG_INPUT_MODIFIER_CTRL);
	key (canvas, GRL_KEY_DELETE, 0);
	g_assert_cmpuint (lrg_timeline_view_get_n_clips (tl, NULL), ==, 0);
	g_assert_cmpuint (removed, ==, 3);

	lrg_timeline_view_set_time (tl, 10.0);
	key (canvas, GRL_KEY_V, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpuint (lrg_timeline_view_get_n_clips (tl, a), ==, 1);
	g_assert_cmpuint (lrg_timeline_view_get_n_clips (tl, b), ==, 2);
	g_assert_cmpuint (lrg_timeline_view_get_clip_at (tl, a, 10.5), !=, LRG_TIMELINE_VIEW_INVALID_ID);
	g_assert_cmpuint (lrg_timeline_view_get_clip_at (tl, b, 12.5), !=, LRG_TIMELINE_VIEW_INVALID_ID);
	g_assert_cmpuint (lrg_timeline_view_get_clip_at (tl, b, 14.5), !=, LRG_TIMELINE_VIEW_INVALID_ID);

	/* Pasted clips are the selection; a plain press in empty space clears it */
	g_clear_pointer (&sel, g_array_unref);
	sel = lrg_timeline_view_get_selected_clips (tl);
	g_assert_cmpuint (sel->len, ==, 3);
	click_at (canvas, XT (0.5), 50.0f);
	g_clear_pointer (&sel, g_array_unref);
	sel = lrg_timeline_view_get_selected_clips (tl);
	g_assert_cmpuint (sel->len, ==, 0);

	/* Esc during a drag puts the clip back */
	c1 = lrg_timeline_view_get_clip_at (tl, b, 12.5);
	lrg_timeline_view_set_scroll_time (tl, 10.0, FALSE);
	drag (canvas, CX + 250.0f, 90.0f, CX + 300.0f, 90.0f, LRG_INPUT_MODIFIER_ALT, FALSE);
	g_assert_cmpfloat (lrg_timeline_view_get_clip_start (tl, c1), !=, 12.0);
	key (canvas, GRL_KEY_ESCAPE, 0);
	g_assert_cmpfloat (lrg_timeline_view_get_clip_start (tl, c1), ==, 12.0);
	frame (canvas, CX + 300.0f, 90.0f, FALSE, FALSE, TRUE, 0);
}

/* ==========================================================================
 * Themes
 * ========================================================================== */

static void
test_presets_smoke (void)
{
	const gchar * const  *presets = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	g_autoptr(LrgTheme)   theme = lrg_theme_new_preset ("midnight");
	LrgTimelineViewTrack *a;
	LrgTimelineViewTrack *b;
	LrgTimelineView      *tl;
	guint                 i;
	guint                 k;
	guint                 shapes;

	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	tl = fixture (canvas, &a, &b);
	lrg_timeline_view_track_set_curve_lane (a, TRUE);
	lrg_timeline_view_add_clip (tl, a, 0.5, 1.0, "intro");
	lrg_timeline_view_add_clip (tl, b, 2.0, 2.0, "music");
	lrg_timeline_view_add_keyframe (tl, a, 0.0, 0.2, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (tl, a, 3.0, 0.9, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	lrg_timeline_view_add_marker (tl, 4.0, "cue");
	lrg_timeline_view_set_loop (tl, TRUE);
	lrg_timeline_view_set_loop_range (tl, 1.0, 3.0);
	lrg_timeline_view_set_time (tl, 1.5);
	lrg_timeline_view_select_clip (tl, lrg_timeline_view_get_clip_at (tl, b, 3.0), FALSE);

	for (i = 0; presets[i] != NULL; i++)
	{
		LrgPaintList *list;

		lrg_theme_apply_preset (theme, presets[i]);
		settle (canvas, 600);
		list = lrg_canvas_record (canvas);

		shapes = 0;
		for (k = 0; k < lrg_paint_list_get_n_instances (list); k++)
			if (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, k)) != LRG_PAINT_PRIM_IMAGE)
				shapes++;
		g_assert_cmpuint (shapes, >, 40);
	}
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define GPU_W (400)
#define GPU_H (200)

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
	test_window = grl_window_new (8, 8, "lrg-ui-timeline-test");
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

/* GPU: the playhead line is the accent colour and a clip its track colour */
static void
test_gpu_timeline (void)
{
	const gchar * const  *presets = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas)  canvas = NULL;
	g_autoptr(LrgTheme)   theme = NULL;
	LrgTimelineView      *tl;
	LrgTimelineViewTrack *t;
	GrlColor              col;
	guint                 i;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = canvas_new ();
	theme = lrg_theme_new_preset ("midnight");
	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	tl = lrg_timeline_view_new ();
	lrg_timeline_view_set_header_width (tl, 100.0f);
	lrg_timeline_view_set_track_height (tl, 40.0f);
	t = lrg_timeline_view_add_track (tl, "A", NULL);
	lrg_timeline_view_add_clip (tl, t, 0.5, 1.0, NULL);
	lrg_timeline_view_track_get_color (t, &col);
	lrg_timeline_view_set_time (tl, 2.0);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (tl));
	g_object_unref (tl);
	lrg_widget_allocate (LRG_WIDGET (tl), 0.0f, 0.0f, (gfloat) GPU_W, (gfloat) GPU_H);
	settle (canvas, 120);

	for (i = 0; presets[i] != NULL; i++)
	{
		Image img;

		lrg_theme_apply_preset (theme, presets[i]);
		settle (canvas, 600);
		img = render_canvas (canvas);

		/* Playhead at x 100 + 200 = 300, below the tracks */
		g_assert_true (pixel_near (&img, 300, 150, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 8));
		/* Clip 0.5..1.5 s -> x 150..250, lower half of row 0 (y 34..66) */
		g_assert_true (pixel_near (&img, 200, 60, &col, 8));
		UnloadImage (img);
	}
}

int
main (int    argc,
      char **argv)
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/ui/timeline/mapping", test_mapping);
	g_test_add_func ("/ui/timeline/wheel-zoom", test_wheel_zoom);
	g_test_add_func ("/ui/timeline/ruler-ticks", test_ruler_ticks);
	g_test_add_func ("/ui/timeline/overlap", test_overlap_rule);
	g_test_add_func ("/ui/timeline/clip-drag-snap", test_clip_drag_snap);
	g_test_add_func ("/ui/timeline/resize-clamps", test_resize_clamps);
	g_test_add_func ("/ui/timeline/keyframes", test_keyframes);
	g_test_add_func ("/ui/timeline/scrub", test_scrub);
	g_test_add_func ("/ui/timeline/marker-drag", test_marker_drag);
	g_test_add_func ("/ui/timeline/playback", test_playback);
	g_test_add_func ("/ui/timeline/collapsed-layout", test_collapsed_layout);
	g_test_add_func ("/ui/timeline/culling", test_culling);
	g_test_add_func ("/ui/timeline/editing", test_editing);
	g_test_add_func ("/ui/timeline/presets", test_presets_smoke);
	g_test_add_func ("/ui/gpu/timeline", test_gpu_timeline);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
