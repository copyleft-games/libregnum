/* lrg-scrollbar-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Header-only helpers shared by the scrolling widgets (LrgScrollBox,
 * LrgListView): a spring-smoothed scroll axis with rubber-band
 * overshoot, overlay scrollbar geometry, fading and painting, and the
 * built-in looks of the SCROLLBAR / SCROLL_THUMB parts.
 *
 * Everything here is static inline so each widget gets its own copy
 * and no extra source file has to be registered with the build.
 */

#pragma once

#include <math.h>
#include <string.h>

#include "lrg-widget.h"
#include "../tween/lrg-spring.h"

G_BEGIN_DECLS

/* Distance one wheel notch scrolls, in canvas units */
#define LRG_SCROLL_WHEEL_STEP (48.0f)

/* Furthest the content may be pulled past an end (rubber band) */
#define LRG_SCROLL_OVERSHOOT (40.0f)

/* Seconds without activity before the overlay scrollbar fades out */
#define LRG_SCROLL_IDLE_HIDE (0.8f)

/* Shortest a scrollbar thumb gets */
#define LRG_SCROLL_MIN_THUMB (24.0f)

/* Gap between the scrollbar and the viewport edge */
#define LRG_SCROLL_BAR_GAP (2.0f)

/* Spring of the scroll offset: period (s) and damping ratio */
#define LRG_SCROLL_RESPONSE (0.22f)
#define LRG_SCROLL_DAMPING (1.0f)

/*
 * LrgScrollAxis:
 *
 * One scrolling direction. @pos.value is the offset that is displayed
 * (where the content is drawn), @pos.target where it is heading. The
 * target may sit slightly outside [0, max] while @bounce is set: the
 * rubber band, which springs back as soon as the offset arrives.
 */
typedef struct
{
	LrgSpring  pos;       /* displayed offset, chasing the target */
	gfloat     content;   /* length of the content */
	gfloat     viewport;  /* length of the visible window */
	gboolean   bounce;    /* target is past an end; return when reached */
	gboolean   dragging;  /* the thumb is being dragged */
	gfloat     grab;      /* pointer offset inside the thumb at grab time */
	LrgWidget *bar;       /* hit zone child of the scrolling widget */
} LrgScrollAxis;

/*
 * LrgScrollFx:
 *
 * Per-widget scrollbar fade. Scrolling widgets step it from their
 * step() vfunc with the canvas frame time: @idle counts the seconds
 * since the last activity, so the bars hide on the canvas clock.
 */
typedef struct
{
	LrgSpring fade;     /* 0 hidden .. 1 shown */
	gboolean  active;   /* there was activity at some point */
	gfloat    idle;     /* seconds since the last scroll / pointer activity */
} LrgScrollFx;

/* ==========================================================================
 * Axis
 * ========================================================================== */

static inline void
lrg_scroll_axis_init (LrgScrollAxis *axis)
{
	memset (axis, 0, sizeof (LrgScrollAxis));
	lrg_spring_init_full (&axis->pos, 0.0f, LRG_SCROLL_RESPONSE, LRG_SCROLL_DAMPING);
}

/* lrg_scroll_axis_get_max: largest valid offset (0 when it all fits) */
static inline gfloat
lrg_scroll_axis_get_max (const LrgScrollAxis *axis)
{
	return MAX (0.0f, axis->content - axis->viewport);
}

/* lrg_scroll_axis_get_value: the displayed offset, rounded to whole units */
static inline gfloat
lrg_scroll_axis_get_value (const LrgScrollAxis *axis)
{
	return roundf (axis->pos.value);
}

/* lrg_scroll_axis_clamp: @value limited to [0, max] */
static inline gfloat
lrg_scroll_axis_clamp (const LrgScrollAxis *axis,
                       gfloat               value)
{
	return CLAMP (value, 0.0f, lrg_scroll_axis_get_max (axis));
}

/*
 * lrg_scroll_axis_set_extent:
 *
 * Records new content / viewport lengths. When the content shrank
 * under the current offset the target is pulled back in range (the
 * displayed offset follows with the spring).
 */
static inline void
lrg_scroll_axis_set_extent (LrgScrollAxis *axis,
                            gfloat         content,
                            gfloat         viewport)
{
	gfloat max;

	axis->content = MAX (0.0f, content);
	axis->viewport = MAX (0.0f, viewport);
	max = lrg_scroll_axis_get_max (axis);

	if (!axis->bounce && (axis->pos.target > max || axis->pos.target < 0.0f))
		lrg_spring_set_target (&axis->pos, lrg_scroll_axis_clamp (axis, axis->pos.target));
}

/*
 * lrg_scroll_axis_scroll_to:
 *
 * Moves to @value (clamped). With @animate the displayed offset glides
 * there; otherwise it jumps.
 */
static inline void
lrg_scroll_axis_scroll_to (LrgScrollAxis *axis,
                           gfloat         value,
                           gboolean       animate)
{
	value = lrg_scroll_axis_clamp (axis, value);
	axis->bounce = FALSE;

	if (animate)
		lrg_spring_set_target (&axis->pos, value);
	else
		lrg_spring_snap (&axis->pos, value);
}

/*
 * lrg_scroll_axis_wheel:
 * @delta: offset change (positive scrolls towards the end)
 * @smooth: glide and rubber-band (FALSE jumps and stops hard at ends)
 *
 * Moves the target by @delta. Pushing past an end parks the target a
 * little beyond it (half the excess, at most LRG_SCROLL_OVERSHOOT) and
 * flags a bounce so lrg_scroll_axis_step() springs it back.
 *
 * Returns: %TRUE if the axis can scroll at all (the event is consumed)
 */
static inline gboolean
lrg_scroll_axis_wheel (LrgScrollAxis *axis,
                       gfloat         delta,
                       gboolean       smooth)
{
	gfloat max = lrg_scroll_axis_get_max (axis);
	gfloat base;
	gfloat target;

	if (max <= 0.0f)
		return FALSE;

	/* Continue from where the content is heading, not from a bounce */
	base = axis->bounce ? lrg_scroll_axis_clamp (axis, axis->pos.target) : axis->pos.target;
	target = base + delta;

	if (!smooth)
	{
		lrg_scroll_axis_scroll_to (axis, target, FALSE);
		return TRUE;
	}

	axis->bounce = FALSE;
	if (target < 0.0f)
	{
		target = -MIN (-target * 0.5f, LRG_SCROLL_OVERSHOOT);
		axis->bounce = TRUE;
	}
	else if (target > max)
	{
		target = max + MIN ((target - max) * 0.5f, LRG_SCROLL_OVERSHOOT);
		axis->bounce = TRUE;
	}

	lrg_spring_set_target (&axis->pos, target);
	return TRUE;
}

/*
 * lrg_scroll_axis_step:
 *
 * Advances the offset spring. A rubber band turns around once the
 * offset is nearly at its overshoot target.
 *
 * Returns: %TRUE while the offset is moving
 */
static inline gboolean
lrg_scroll_axis_step (LrgScrollAxis *axis,
                      gfloat         delta_time)
{
	gboolean moving = lrg_spring_step (&axis->pos, delta_time);

	if (axis->bounce && fabsf (axis->pos.value - axis->pos.target) < 1.5f)
	{
		axis->bounce = FALSE;
		lrg_spring_set_target (&axis->pos, lrg_scroll_axis_clamp (axis, axis->pos.target));
		moving = TRUE;
	}
	return moving || axis->bounce;
}

/*
 * lrg_scroll_axis_thumb:
 * @track_start: where the track begins
 * @track_len: track length
 * @start: (out): thumb start
 * @len: (out): thumb length
 *
 * Thumb geometry: as long as the visible fraction of the content, at
 * least LRG_SCROLL_MIN_THUMB, and squashed against the end while the
 * content is pulled past it.
 */
static inline void
lrg_scroll_axis_thumb (const LrgScrollAxis *axis,
                       gfloat               track_start,
                       gfloat               track_len,
                       gfloat              *start,
                       gfloat              *len)
{
	gfloat max = lrg_scroll_axis_get_max (axis);
	gfloat value = axis->pos.value;
	gfloat l;
	gfloat over = 0.0f;
	gfloat frac;

	if (axis->content <= 0.0f || max <= 0.0f || track_len <= 0.0f)
	{
		*start = track_start;
		*len = track_len;
		return;
	}

	l = track_len * (axis->viewport / axis->content);
	l = CLAMP (l, MIN (LRG_SCROLL_MIN_THUMB, track_len), track_len);

	/* Rubber band: shrink the thumb by the overshoot */
	if (value < 0.0f)
		over = -value;
	else if (value > max)
		over = value - max;
	l = MAX (MIN (l, LRG_SCROLL_MIN_THUMB * 0.5f), l - over);

	frac = CLAMP (value / max, 0.0f, 1.0f);
	*start = track_start + (track_len - l) * frac;
	*len = l;
}

/*
 * lrg_scroll_axis_offset_for_thumb:
 *
 * Inverse of lrg_scroll_axis_thumb(): the offset that puts the thumb
 * start at @thumb_start (used while dragging).
 */
static inline gfloat
lrg_scroll_axis_offset_for_thumb (const LrgScrollAxis *axis,
                                  gfloat               track_start,
                                  gfloat               track_len,
                                  gfloat               thumb_start)
{
	gfloat ts;
	gfloat tl;
	gfloat room;

	lrg_scroll_axis_thumb (axis, track_start, track_len, &ts, &tl);
	room = track_len - tl;
	if (room <= 0.0f)
		return 0.0f;
	return lrg_scroll_axis_clamp (axis, (thumb_start - track_start) / room *
	                                    lrg_scroll_axis_get_max (axis));
}

/* ==========================================================================
 * Fading
 * ========================================================================== */

static inline void
lrg_scroll_fx_init (LrgScrollFx *fx)
{
	memset (fx, 0, sizeof (LrgScrollFx));
	lrg_spring_init (&fx->fade, 0.0f, LRG_MOTION_SMOOTH);
}

/* lrg_scroll_fx_poke: something happened; keep the scrollbars shown */
static inline void
lrg_scroll_fx_poke (LrgScrollFx *fx)
{
	fx->active = TRUE;
	fx->idle = 0.0f;
}

/*
 * lrg_scroll_fx_update:
 * @keep: force the bars visible (hovered, dragging)
 * @delta_time: canvas frame time (seconds)
 *
 * Counts the idle time, aims the fade at 1 while there was activity in
 * the last LRG_SCROLL_IDLE_HIDE seconds (or @keep), else at 0, and
 * steps it.
 *
 * Returns: %TRUE while the fade moves, or while the idle countdown runs
 *   (the bars must start fading out when it expires, which takes a
 *   frame); %FALSE once the bars rest, shown (kept) or hidden
 */
static inline gboolean
lrg_scroll_fx_update (LrgScrollFx *fx,
                      gboolean     keep,
                      gfloat       delta_time)
{
	gboolean recent;
	gboolean moving;

	/* Idle time saturates past the threshold; nothing changes after */
	if (fx->active && delta_time > 0.0f)
		fx->idle = MIN (fx->idle + delta_time, LRG_SCROLL_IDLE_HIDE * 2.0f);
	recent = fx->active && fx->idle < LRG_SCROLL_IDLE_HIDE;

	lrg_spring_set_target (&fx->fade, (keep || recent) ? 1.0f : 0.0f);
	moving = lrg_spring_step (&fx->fade, delta_time);
	return moving || (recent && !keep);
}

/* ==========================================================================
 * Looks and painting
 * ========================================================================== */

/*
 * lrg_scroll_style_defaults:
 *
 * Built-in looks of the overlay scrollbar. The track (SCROLLBAR) is a
 * faint TRACK-coloured groove; the thumb is a pill in muted text
 * colour (readable on every preset), firmer when hovered and accent
 * coloured while dragged.
 *
 * Returns: %TRUE if @part is a scrollbar part
 */
static inline gboolean
lrg_scroll_style_defaults (LrgTheme       *theme,
                           LrgUIPart       part,
                           LrgWidgetState  state,
                           LrgPartLook    *look)
{
	gfloat   size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_SCROLLBAR_SIZE);
	GrlColor c;

	if (part == LRG_UI_PART_SCROLLBAR)
	{
		const GrlColor *track = lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK);

		lrg_paint_color_with_alpha (track, ((gfloat) track->a / 255.0f) * 0.45f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_radius (look, size);
		lrg_part_look_set_size (look, size);
		return TRUE;
	}

	if (part == LRG_UI_PART_SCROLL_THUMB)
	{
		const GrlColor *muted = lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED);
		gfloat          alpha = 0.55f;

		if (state & LRG_WIDGET_STATE_HOVERED)
			alpha = 0.85f;
		lrg_paint_color_with_alpha (muted, alpha, &c);
		if (state & LRG_WIDGET_STATE_PRESSED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_radius (look, size);
		lrg_part_look_set_size (look, size);
		return TRUE;
	}

	return FALSE;
}

/*
 * lrg_scroll_bar_zone:
 *
 * Thickness of the hit zone along a viewport edge that belongs to a
 * scrollbar.
 */
static inline gfloat
lrg_scroll_bar_zone (LrgWidget *owner)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (owner), LRG_THEME_METRIC_SCROLLBAR_SIZE) +
	       LRG_SCROLL_BAR_GAP * 2.0f;
}

/*
 * lrg_scroll_bar_track:
 * @vertical: vertical (TRUE) or horizontal bar
 * @start: (out): track start along the axis, world space
 * @len: (out): track length
 *
 * The track runs along the bar's hit zone, inset by the gap.
 */
static inline void
lrg_scroll_bar_track (const LrgScrollAxis *axis,
                      gboolean             vertical,
                      gfloat              *start,
                      gfloat              *len)
{
	LrgWidget *bar = axis->bar;

	if (vertical)
	{
		*start = lrg_widget_get_world_y (bar) + LRG_SCROLL_BAR_GAP;
		*len = MAX (0.0f, lrg_widget_get_height (bar) - LRG_SCROLL_BAR_GAP * 2.0f);
	}
	else
	{
		*start = lrg_widget_get_world_x (bar) + LRG_SCROLL_BAR_GAP;
		*len = MAX (0.0f, lrg_widget_get_width (bar) - LRG_SCROLL_BAR_GAP * 2.0f);
	}
}

/*
 * lrg_scroll_bar_paint:
 * @owner: the scrolling widget (its looks are used)
 * @list: paint list
 * @axis: the axis whose bar is drawn
 * @vertical: orientation of the bar
 * @fade: overall visibility 0..1
 *
 * Draws one overlay scrollbar inside its hit zone: a pill that is thin
 * at rest and grows to the full SCROLLBAR_SIZE when its zone is
 * hovered, over a groove that only shows while hovered. Thumb colours
 * blend between the resting, hovered and pressed looks.
 */
static inline void
lrg_scroll_bar_paint (LrgWidget           *owner,
                      LrgPaintList        *list,
                      const LrgScrollAxis *axis,
                      gboolean             vertical,
                      gfloat               fade)
{
	LrgWidget   *bar = axis->bar;
	LrgPartLook  track;
	LrgPartLook  thumb;
	LrgPartLook  variant;
	gfloat       hover;
	gfloat       press;
	gfloat       thick;
	gfloat       start;
	gfloat       len;
	gfloat       ts;
	gfloat       tl;
	gfloat       zx;
	gfloat       zy;
	gfloat       zw;
	gfloat       zh;
	gfloat       cross;

	if (bar == NULL || !lrg_widget_get_visible (bar) || fade <= 0.01f)
		return;

	hover = CLAMP (lrg_widget_get_hover_progress (bar), 0.0f, 1.0f);
	press = axis->dragging ? 1.0f : CLAMP (lrg_widget_get_press_progress (bar), 0.0f, 1.0f);
	hover = MAX (hover, press);

	/* Looks: groove, and the thumb blended by hover / press */
	lrg_widget_resolve_look_for_state (owner, LRG_UI_PART_SCROLLBAR, LRG_WIDGET_STATE_NONE, &track);
	lrg_widget_resolve_look_for_state (owner, LRG_UI_PART_SCROLL_THUMB, LRG_WIDGET_STATE_NONE, &thumb);
	if (hover > 0.001f)
	{
		lrg_widget_resolve_look_for_state (owner, LRG_UI_PART_SCROLL_THUMB,
		                                   LRG_WIDGET_STATE_HOVERED, &variant);
		lrg_part_look_lerp (&thumb, &variant, hover, &thumb);
	}
	if (press > 0.001f)
	{
		lrg_widget_resolve_look_for_state (owner, LRG_UI_PART_SCROLL_THUMB,
		                                   LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED,
		                                   &variant);
		lrg_part_look_lerp (&thumb, &variant, press, &thumb);
	}

	/* Thin at rest, full size when the zone is hovered */
	thick = MAX (2.0f, thumb.size * (0.5f + 0.5f * hover));

	zx = lrg_widget_get_world_x (bar);
	zy = lrg_widget_get_world_y (bar);
	zw = lrg_widget_get_width (bar);
	zh = lrg_widget_get_height (bar);
	lrg_scroll_bar_track (axis, vertical, &start, &len);
	lrg_scroll_axis_thumb (axis, start, len, &ts, &tl);

	lrg_paint_list_push_opacity (list, CLAMP (fade, 0.0f, 1.0f));

	/* Groove (hover only), hugging the outer edge of the zone */
	if (hover > 0.01f)
	{
		gfloat gt = MAX (2.0f, track.size);

		lrg_paint_list_push_opacity (list, hover);
		if (vertical)
		{
			cross = zx + zw - LRG_SCROLL_BAR_GAP - gt;
			lrg_widget_paint_box (owner, list, &track, cross, start, gt, len);
		}
		else
		{
			cross = zy + zh - LRG_SCROLL_BAR_GAP - gt;
			lrg_widget_paint_box (owner, list, &track, start, cross, len, gt);
		}
		lrg_paint_list_pop_opacity (list);
	}

	/* Thumb */
	if (vertical)
	{
		cross = zx + zw - LRG_SCROLL_BAR_GAP - thick;
		lrg_widget_paint_box (owner, list, &thumb, cross, ts, thick, tl);
	}
	else
	{
		cross = zy + zh - LRG_SCROLL_BAR_GAP - thick;
		lrg_widget_paint_box (owner, list, &thumb, ts, cross, tl, thick);
	}

	lrg_paint_list_pop_opacity (list);
}

/*
 * lrg_scroll_bar_press:
 * @axis: the axis whose bar zone was pressed
 * @vertical: orientation
 * @pos: pointer coordinate along the axis, world space
 * @smooth: animate page jumps
 *
 * A press on the thumb starts a drag; a press on the track pages
 * towards the pointer by 90% of the viewport.
 */
static inline void
lrg_scroll_bar_press (LrgScrollAxis *axis,
                      gboolean       vertical,
                      gfloat         pos,
                      gboolean       smooth)
{
	gfloat start;
	gfloat len;
	gfloat ts;
	gfloat tl;
	gfloat page = MAX (1.0f, axis->viewport * 0.9f);

	lrg_scroll_bar_track (axis, vertical, &start, &len);
	lrg_scroll_axis_thumb (axis, start, len, &ts, &tl);

	if (pos >= ts && pos <= ts + tl)
	{
		axis->dragging = TRUE;
		axis->grab = pos - ts;
		return;
	}

	lrg_scroll_axis_scroll_to (axis, axis->pos.target + (pos < ts ? -page : page), smooth);
}

/*
 * lrg_scroll_bar_drag:
 *
 * Follows the pointer while the thumb is dragged (no smoothing: the
 * thumb sticks to the pointer).
 */
static inline void
lrg_scroll_bar_drag (LrgScrollAxis *axis,
                     gboolean       vertical,
                     gfloat         pos)
{
	gfloat start;
	gfloat len;

	if (!axis->dragging)
		return;
	lrg_scroll_bar_track (axis, vertical, &start, &len);
	lrg_scroll_axis_scroll_to (axis,
	                           lrg_scroll_axis_offset_for_thumb (axis, start, len, pos - axis->grab),
	                           FALSE);
}

/*
 * lrg_scroll_bar_new:
 *
 * A plain, invisible widget used as the scrollbar's hit zone. Kept as
 * the last children of the scrolling widget so it is hit before the
 * content; its events bubble to the owner, which recognises it as the
 * event target.
 */
static inline LrgWidget *
lrg_scroll_bar_new (const gchar *name)
{
	LrgWidget *bar = g_object_new (LRG_TYPE_WIDGET, NULL);

	lrg_widget_set_name (bar, name);
	lrg_widget_set_visible (bar, FALSE);
	return bar;
}

G_END_DECLS
