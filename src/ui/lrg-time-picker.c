/* lrg-time-picker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTimePicker: Hour / minute / second spin segments with 12/24-hour modes.
 *
 * One field-like widget (BOX part) holding up to four segments: hour,
 * minute, optional second and, in 12-hour mode, an AM/PM toggle. Each
 * segment is drawn with the ITEM part (per-segment hover spring, and
 * the SELECTED look on the active segment) and edits like a tiny spin
 * box: Up / Down, the wheel, a vertical drag or typed digits change it;
 * Left / Right move between segments. A changed digit rolls in from
 * above or below on a per-segment spring.
 *
 * The time is stored as 24-hour hour / minute / second; the 12-hour
 * display is computed. Stepping a field wraps inside the field and does
 * not carry into the next one.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <stdio.h>

#include "lrg-time-picker.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

#define N_SEGMENTS (4)

/* Pixels of vertical drag per step */
#define DRAG_STEP (10.0f)

typedef struct
{
	LrgTimePickerSegment segment;
	gfloat               x;       /* relative to the widget */
	gfloat               width;
} SegmentSlot;

struct _LrgTimePicker
{
	LrgWidget             parent_instance;

	gint                  hour;
	gint                  minute;
	gint                  second;
	gboolean              show_seconds;
	gboolean              use_24h;

	LrgTimePickerSegment  active;
	gint                  hover;          /* segment under the pointer, -1 */
	gint                  typed;          /* first typed digit waiting, -1 */

	/* Vertical drag on a segment */
	gboolean              dragging;
	gint                  drag_segment;
	gfloat                drag_y;
	gint                  drag_steps;

	/* Per segment: hover highlight and digit roll (-1..1, 0 = at rest) */
	LrgSpring             hover_spring[N_SEGMENTS];
	LrgSpring             roll[N_SEGMENTS];
	gboolean              stepped;
};

G_DEFINE_FINAL_TYPE (LrgTimePicker, lrg_time_picker, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_HOUR,
	PROP_MINUTE,
	PROP_SECOND,
	PROP_SHOW_SECONDS,
	PROP_USE_24H,
	N_PROPS
};

enum
{
	SIGNAL_TIME_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

GType
lrg_time_picker_segment_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TIME_PICKER_SEGMENT_HOUR, "LRG_TIME_PICKER_SEGMENT_HOUR", "hour" },
			{ LRG_TIME_PICKER_SEGMENT_MINUTE, "LRG_TIME_PICKER_SEGMENT_MINUTE", "minute" },
			{ LRG_TIME_PICKER_SEGMENT_SECOND, "LRG_TIME_PICKER_SEGMENT_SECOND", "second" },
			{ LRG_TIME_PICKER_SEGMENT_PERIOD, "LRG_TIME_PICKER_SEGMENT_PERIOD", "period" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgTimePickerSegment"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static GrlColor
transparent (const GrlColor *color)
{
	GrlColor c = *color;

	c.a = 0;
	return c;
}

static gfloat
font_size (LrgWidget *widget)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (widget), LRG_THEME_METRIC_FONT_SIZE);
}

static gfloat
text_width (LrgWidget   *widget,
            const gchar *text)
{
	gfloat w = 0.0f;

	lrg_paint_measure_text (lrg_theme_get_ui_font (lrg_widget_get_theme (widget)), text,
	                        font_size (widget), 1.0f, &w, NULL);
	return w;
}

/* segment_shown: whether @segment is part of the current display */
static gboolean
segment_shown (LrgTimePicker        *self,
               LrgTimePickerSegment  segment)
{
	if (segment == LRG_TIME_PICKER_SEGMENT_SECOND)
		return self->show_seconds;
	if (segment == LRG_TIME_PICKER_SEGMENT_PERIOD)
		return !self->use_24h;
	return TRUE;
}

/* display_hour: the hour as shown (0..23, or 1..12) */
static gint
display_hour (LrgTimePicker *self)
{
	gint h;

	if (self->use_24h)
		return self->hour;
	h = self->hour % 12;
	return h == 0 ? 12 : h;
}

/* segment_text: the text of @segment into @buf */
static void
segment_text (LrgTimePicker        *self,
              LrgTimePickerSegment  segment,
              gchar                *buf,
              gsize                 size)
{
	switch (segment)
	{
	case LRG_TIME_PICKER_SEGMENT_HOUR:
		g_snprintf (buf, size, "%02d", display_hour (self));
		break;
	case LRG_TIME_PICKER_SEGMENT_MINUTE:
		g_snprintf (buf, size, "%02d", self->minute);
		break;
	case LRG_TIME_PICKER_SEGMENT_SECOND:
		g_snprintf (buf, size, "%02d", self->second);
		break;
	case LRG_TIME_PICKER_SEGMENT_PERIOD:
	default:
		g_snprintf (buf, size, "%s", self->hour >= 12 ? "PM" : "AM");
		break;
	}
}

/*
 * layout_segments:
 *
 * The shown segments left to right with their x and width. Number
 * fields are as wide as "00" plus padding, the period as wide as "PM";
 * colons sit in the gaps between number fields, the period is set off
 * by the theme spacing.
 */
static guint
layout_segments (LrgTimePicker *self,
                 SegmentSlot    slots[N_SEGMENTS])
{
	LrgWidget *widget = LRG_WIDGET (self);
	LrgTheme  *theme = lrg_widget_get_theme (widget);
	gfloat     pad = roundf (lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X) * 0.35f);
	gfloat     outer = roundf (lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X) * 0.5f);
	gfloat     num_w = roundf (text_width (widget, "00") + pad * 2.0f);
	gfloat     colon = roundf (text_width (widget, ":") + 4.0f);
	gfloat     x = outer;
	guint      n = 0;
	gint       s;

	for (s = 0; s < N_SEGMENTS; s++)
	{
		if (!segment_shown (self, (LrgTimePickerSegment) s))
			continue;
		if (s == LRG_TIME_PICKER_SEGMENT_PERIOD)
		{
			x += lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING);
			slots[n].segment = (LrgTimePickerSegment) s;
			slots[n].x = x;
			slots[n].width = roundf (text_width (widget, "PM") + pad * 2.0f);
		}
		else
		{
			if (n > 0)
				x += colon;
			slots[n].segment = (LrgTimePickerSegment) s;
			slots[n].x = x;
			slots[n].width = num_w;
		}
		x += slots[n].width;
		n++;
	}
	return n;
}

/* segment_at: the segment under canvas point @px, -1 for none */
static gint
segment_at (LrgTimePicker *self,
            gfloat         px)
{
	SegmentSlot slots[N_SEGMENTS];
	guint       n = layout_segments (self, slots);
	gfloat      rel = px - lrg_widget_get_world_x (LRG_WIDGET (self));
	guint       i;

	for (i = 0; i < n; i++)
		if (rel >= slots[i].x - 2.0f && rel < slots[i].x + slots[i].width + 2.0f)
			return (gint) slots[i].segment;
	return -1;
}

/*
 * apply_time:
 *
 * Stores a new time, notifies each changed field, rolls the digits
 * that changed (direction @dir, 0 for none) and emits time-changed
 * once.
 */
static void
apply_time (LrgTimePicker *self,
            gint           hour,
            gint           minute,
            gint           second,
            gint           dir)
{
	gboolean changed = FALSE;
	gboolean motion = self->stepped && lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;

	hour = CLAMP (hour, 0, 23);
	minute = CLAMP (minute, 0, 59);
	second = CLAMP (second, 0, 59);

	g_object_freeze_notify (G_OBJECT (self));
	if (hour != self->hour)
	{
		gint     old_display = display_hour (self);
		gboolean old_pm = self->hour >= 12;

		self->hour = hour;
		/* Roll only what visibly changed (AM/PM flips keep the digits) */
		if (motion && dir != 0 && display_hour (self) != old_display)
			self->roll[LRG_TIME_PICKER_SEGMENT_HOUR].value = (gfloat) -dir;
		if (motion && dir != 0 && (hour >= 12) != old_pm)
			self->roll[LRG_TIME_PICKER_SEGMENT_PERIOD].value = (gfloat) -dir;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_HOUR]);
		changed = TRUE;
	}
	if (minute != self->minute)
	{
		if (motion && dir != 0)
			self->roll[LRG_TIME_PICKER_SEGMENT_MINUTE].value = (gfloat) -dir;
		self->minute = minute;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MINUTE]);
		changed = TRUE;
	}
	if (second != self->second)
	{
		if (motion && dir != 0)
			self->roll[LRG_TIME_PICKER_SEGMENT_SECOND].value = (gfloat) -dir;
		self->second = second;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SECOND]);
		changed = TRUE;
	}
	g_object_thaw_notify (G_OBJECT (self));

	if (changed)
		g_signal_emit (self, signals[SIGNAL_TIME_CHANGED], 0);
}

/* wrap: @value + @delta wrapped into 0..@n-1 */
static gint
wrap (gint value,
      gint delta,
      gint n)
{
	gint v = (value + delta) % n;

	return v < 0 ? v + n : v;
}

/*
 * step_segment:
 *
 * The heart of every spin gesture; see lrg_time_picker_step_segment().
 * In 12-hour mode the hour cycles 12, 1..11 inside the current half of
 * the day.
 */
static void
step_segment (LrgTimePicker        *self,
              LrgTimePickerSegment  segment,
              gint                  delta)
{
	gint h = self->hour;
	gint m = self->minute;
	gint s = self->second;
	gint dir = delta > 0 ? 1 : (delta < 0 ? -1 : 0);

	if (delta == 0)
		return;

	switch (segment)
	{
	case LRG_TIME_PICKER_SEGMENT_HOUR:
		if (self->use_24h)
			h = wrap (h, delta, 24);
		else
			h = wrap (h % 12, delta, 12) + (h >= 12 ? 12 : 0);
		break;
	case LRG_TIME_PICKER_SEGMENT_MINUTE:
		m = wrap (m, delta, 60);
		break;
	case LRG_TIME_PICKER_SEGMENT_SECOND:
		s = wrap (s, delta, 60);
		break;
	case LRG_TIME_PICKER_SEGMENT_PERIOD:
	default:
		if (delta % 2 != 0)
			h = (h + 12) % 24;
		break;
	}
	apply_time (self, h, m, s, dir);
}

/*
 * type_digit:
 *
 * A digit typed on the active number segment. The first digit sets the
 * field (when valid) and waits for a second one; two digits that make
 * a valid value set it and move on to the next segment. A first digit
 * that cannot start a two-digit value moves on at once.
 */
static void
type_digit (LrgTimePicker *self,
            gint           digit)
{
	gint lo = 0;
	gint hi = 59;
	gint value;

	if (self->active == LRG_TIME_PICKER_SEGMENT_PERIOD)
		return;
	if (self->active == LRG_TIME_PICKER_SEGMENT_HOUR)
	{
		lo = self->use_24h ? 0 : 1;
		hi = self->use_24h ? 23 : 12;
	}

	value = self->typed >= 0 ? self->typed * 10 + digit : digit;
	if (self->typed >= 0 && (value < lo || value > hi))
	{
		/* Not a valid pair: start again from this digit */
		self->typed = -1;
		value = digit;
	}

	if (value >= lo && value <= hi)
	{
		gint h = self->hour;
		gint m = self->minute;
		gint s = self->second;

		switch (self->active)
		{
		case LRG_TIME_PICKER_SEGMENT_HOUR:
			h = self->use_24h ? value : (value % 12) + (self->hour >= 12 ? 12 : 0);
			break;
		case LRG_TIME_PICKER_SEGMENT_MINUTE:
			m = value;
			break;
		default:
			s = value;
			break;
		}
		apply_time (self, h, m, s, 1);
	}

	if (self->typed >= 0 || value * 10 > hi)
	{
		/* Field complete: next segment */
		gint next;

		self->typed = -1;
		for (next = (gint) self->active + 1; next < N_SEGMENTS; next++)
			if (segment_shown (self, (LrgTimePickerSegment) next))
			{
				self->active = (LrgTimePickerSegment) next;
				break;
			}
	}
	else
		self->typed = digit;
}

/* move_active: Left / Right between shown segments */
static gboolean
move_active (LrgTimePicker *self,
             gint           delta)
{
	gint s = (gint) self->active + delta;

	self->typed = -1;
	while (s >= 0 && s < N_SEGMENTS)
	{
		if (segment_shown (self, (LrgTimePickerSegment) s))
		{
			self->active = (LrgTimePickerSegment) s;
			return TRUE;
		}
		s += delta;
	}
	return FALSE;
}

/* ==========================================================================
 * LrgWidget vfuncs
 * ========================================================================== */

static void
lrg_time_picker_style_defaults (LrgWidget      *widget,
                                LrgUIPart       part,
                                LrgWidgetState  state,
                                LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_BOX:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
		lrg_part_look_set_border (look,
		                          lrg_theme_get_color (theme, (state & LRG_WIDGET_STATE_FOCUSED)
		                                                      ? LRG_THEME_COLOR_ACCENT
		                                                      : (state & LRG_WIDGET_STATE_HOVERED)
		                                                        ? LRG_THEME_COLOR_TEXT_MUTED
		                                                        : LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		break;

	case LRG_UI_PART_ITEM:
		{
			const GrlColor *hover = lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
			GrlColor        rest = transparent (hover);

			/* The active segment of a focused picker is SELECTED:
			 * accent fill; others are flat until hovered / pressed */
			if (state & LRG_WIDGET_STATE_SELECTED)
			{
				lrg_part_look_set_background (look, lrg_theme_get_color (theme,
				                                                         (state & LRG_WIDGET_STATE_PRESSED)
				                                                         ? LRG_THEME_COLOR_ACCENT_PRESSED
				                                                         : (state & LRG_WIDGET_STATE_HOVERED)
				                                                           ? LRG_THEME_COLOR_ACCENT_HOVER
				                                                           : LRG_THEME_COLOR_ACCENT));
				lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
				                                                         LRG_THEME_COLOR_TEXT_ON_ACCENT));
			}
			else
			{
				if (state & LRG_WIDGET_STATE_PRESSED)
					lrg_part_look_set_background (look, lrg_theme_get_color (theme,
					                                                         LRG_THEME_COLOR_CONTROL_PRESSED));
				else if (state & LRG_WIDGET_STATE_HOVERED)
					lrg_part_look_set_background (look, hover);
				else
					lrg_part_look_set_background (look, &rest);
				lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
			}
			lrg_part_look_set_radius (look,
			                          MAX (2.0f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) -
			                                     2.0f));
		}
		break;

	case LRG_UI_PART_LABEL:
		/* The ":" separators */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		break;

	default:
		LRG_WIDGET_CLASS (lrg_time_picker_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_time_picker_measure (LrgWidget *widget,
                         gfloat    *width,
                         gfloat    *height)
{
	LrgTimePicker *self = LRG_TIME_PICKER (widget);
	SegmentSlot    slots[N_SEGMENTS];
	guint          n = layout_segments (self, slots);
	gfloat         outer = roundf (lrg_theme_get_metric (lrg_widget_get_theme (widget),
	                                                     LRG_THEME_METRIC_PADDING_X) * 0.5f);

	*width = n > 0 ? slots[n - 1].x + slots[n - 1].width + outer : outer * 2.0f;
	*height = lrg_theme_get_metric (lrg_widget_get_theme (widget), LRG_THEME_METRIC_CONTROL_HEIGHT);
}

static gboolean
lrg_time_picker_step (LrgWidget *widget,
                      gfloat     delta_time)
{
	LrgTimePicker *self = LRG_TIME_PICKER (widget);
	LrgMotion      motion = lrg_widget_get_motion (widget);
	gboolean       busy = FALSE;
	gint           s;

	for (s = 0; s < N_SEGMENTS; s++)
	{
		lrg_spring_set_motion (&self->hover_spring[s], motion);
		lrg_spring_set_target (&self->hover_spring[s], self->hover == s ? 1.0f : 0.0f);
		lrg_spring_set_motion (&self->roll[s], motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
		lrg_spring_set_target (&self->roll[s], 0.0f);

		if (!self->stepped || motion == LRG_MOTION_NONE)
		{
			lrg_spring_snap (&self->hover_spring[s], self->hover_spring[s].target);
			lrg_spring_snap (&self->roll[s], 0.0f);
			continue;
		}
		busy |= lrg_spring_step (&self->hover_spring[s], delta_time);
		busy |= lrg_spring_step (&self->roll[s], delta_time);
	}
	self->stepped = TRUE;
	return busy;
}

static void
lrg_time_picker_paint (LrgWidget    *widget,
                       LrgPaintList *list)
{
	LrgTimePicker  *self = LRG_TIME_PICKER (widget);
	LrgTheme       *theme = lrg_widget_get_theme (widget);
	GrlFont        *font = lrg_theme_get_ui_font (theme);
	LrgWidgetState  wstate = lrg_widget_get_state_flags (widget);
	LrgPartLook     box;
	LrgPartLook     sep;
	SegmentSlot     slots[N_SEGMENTS];
	guint           n = layout_segments (self, slots);
	gfloat          x = lrg_widget_get_world_x (widget);
	gfloat          y = lrg_widget_get_world_y (widget);
	gfloat          w = lrg_widget_get_width (widget);
	gfloat          h = lrg_widget_get_height (widget);
	gfloat          size = font_size (widget);
	gfloat          inset = roundf (h * 0.14f);
	gfloat          th = size;
	guint           i;

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, x, y, w, h);
	if (box.opacity < 1.0f)
		lrg_paint_list_push_opacity (list, box.opacity);

	lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &sep);
	lrg_paint_measure_text (font, "00", size, 1.0f, NULL, &th);

	for (i = 0; i < n; i++)
	{
		SegmentSlot    *slot = &slots[i];
		LrgWidgetState  state = wstate & LRG_WIDGET_STATE_DISABLED;
		LrgPartLook     rest;
		LrgPartLook     lit;
		LrgPartLook     look;
		gchar           text[8];
		gfloat          tw = 0.0f;
		gfloat          roll = self->stepped ? self->roll[slot->segment].value : 0.0f;
		gfloat          hv = self->stepped ? self->hover_spring[slot->segment].value
		                                   : (self->hover == (gint) slot->segment ? 1.0f : 0.0f);
		gfloat          sx = x + slot->x;
		gfloat          sy = y + inset;
		gfloat          sh = h - inset * 2.0f;

		/* ":" before number fields after the first */
		if (i > 0 && slot->segment != LRG_TIME_PICKER_SEGMENT_PERIOD)
		{
			gfloat cw = 0.0f;
			gfloat gap_l = slots[i - 1].x + slots[i - 1].width;

			lrg_paint_measure_text (font, ":", size, 1.0f, &cw, NULL);
			lrg_paint_list_text (list, font, ":", roundf (x + (gap_l + slot->x - cw) * 0.5f),
			                     roundf (y + (h - th) * 0.5f - 1.0f), size, 1.0f, &sep.foreground);
		}

		/* Segment highlight: hover spring between rest and hovered */
		if (slot->segment == self->active && (wstate & LRG_WIDGET_STATE_FOCUSED))
			state |= LRG_WIDGET_STATE_SELECTED;
		if (self->dragging && self->drag_segment == (gint) slot->segment)
			state |= LRG_WIDGET_STATE_PRESSED;
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, state, &rest);
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, state | LRG_WIDGET_STATE_HOVERED, &lit);
		lrg_part_look_lerp (&rest, &lit, CLAMP (hv, 0.0f, 1.0f), &look);
		lrg_widget_paint_box (widget, list, &look, sx, sy, slot->width, sh);

		/* Digits, rolled in from above / below after a change */
		segment_text (self, slot->segment, text, sizeof (text));
		lrg_paint_measure_text (font, text, size, 1.0f, &tw, NULL);
		lrg_paint_list_push_clip (list, sx, sy, slot->width, sh);
		if (roll != 0.0f)
			lrg_paint_list_push_opacity (list, CLAMP (1.0f - fabsf (roll), 0.0f, 1.0f));
		lrg_paint_list_text (list, font, text, roundf (sx + (slot->width - tw) * 0.5f),
		                     roundf (y + (h - th) * 0.5f + roll * sh * 0.6f), size, 1.0f, &look.foreground);
		if (roll != 0.0f)
			lrg_paint_list_pop_opacity (list);
		lrg_paint_list_pop_clip (list);
	}

	if (box.opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

static gboolean
lrg_time_picker_handle_event (LrgWidget        *widget,
                              const LrgUIEvent *event)
{
	LrgTimePicker *self = LRG_TIME_PICKER (widget);
	gfloat         ex = lrg_ui_event_get_x (event);
	gfloat         ey = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover = lrg_widget_contains_point (widget, ex, ey) ? segment_at (self, ex) : -1;
		if (self->dragging && self->drag_segment != LRG_TIME_PICKER_SEGMENT_PERIOD)
		{
			/* Drag up to increase; apply only the new whole steps */
			gint steps = (gint) floorf ((self->drag_y - ey) / DRAG_STEP);

			if (steps != self->drag_steps)
			{
				step_segment (self, (LrgTimePickerSegment) self->drag_segment, steps - self->drag_steps);
				self->drag_steps = steps;
			}
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover = -1;
		return FALSE;

	case LRG_UI_EVENT_FOCUS_OUT:
		self->typed = -1;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			gint seg;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			seg = segment_at (self, ex);
			if (seg < 0)
				return TRUE;
			self->active = (LrgTimePickerSegment) seg;
			self->typed = -1;
			self->dragging = TRUE;
			self->drag_segment = seg;
			self->drag_y = ey;
			self->drag_steps = 0;
			return TRUE;
		}

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		/* A click (no drag) on AM / PM toggles it */
		if (self->dragging && self->drag_segment == LRG_TIME_PICKER_SEGMENT_PERIOD &&
		    segment_at (self, ex) == LRG_TIME_PICKER_SEGMENT_PERIOD)
			step_segment (self, LRG_TIME_PICKER_SEGMENT_PERIOD, 1);
		self->dragging = FALSE;
		return TRUE;

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat dy = lrg_ui_event_get_scroll_y (event);
			gint   seg = segment_at (self, ex);

			if (dy == 0.0f)
				return FALSE;
			if (seg < 0)
				seg = (gint) self->active;
			/* Wheel up (positive) increases */
			step_segment (self, (LrgTimePickerSegment) seg, dy > 0.0f ? 1 : -1);
			return TRUE;
		}

	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_UP:
			self->typed = -1;
			step_segment (self, self->active, 1);
			return TRUE;
		case GRL_KEY_DOWN:
			self->typed = -1;
			step_segment (self, self->active, -1);
			return TRUE;
		case GRL_KEY_PAGE_UP:
			self->typed = -1;
			step_segment (self, self->active, self->active == LRG_TIME_PICKER_SEGMENT_HOUR ? 6 : 10);
			return TRUE;
		case GRL_KEY_PAGE_DOWN:
			self->typed = -1;
			step_segment (self, self->active, self->active == LRG_TIME_PICKER_SEGMENT_HOUR ? -6 : -10);
			return TRUE;
		case GRL_KEY_LEFT:
			return move_active (self, -1);
		case GRL_KEY_RIGHT:
			return move_active (self, 1);
		default:
			return FALSE;
		}

	case LRG_UI_EVENT_ACTIVATE:
		/* Enter / A toggles AM / PM on the period, else moves on */
		if (self->active == LRG_TIME_PICKER_SEGMENT_PERIOD)
			step_segment (self, LRG_TIME_PICKER_SEGMENT_PERIOD, 1);
		else
			move_active (self, 1);
		return TRUE;

	case LRG_UI_EVENT_TEXT_INPUT:
		{
			gunichar c = lrg_ui_event_get_codepoint (event);

			if (c >= '0' && c <= '9')
			{
				type_digit (self, (gint) (c - '0'));
				return TRUE;
			}
			if (!self->use_24h && (c == 'a' || c == 'A') && self->hour >= 12)
			{
				step_segment (self, LRG_TIME_PICKER_SEGMENT_PERIOD, 1);
				return TRUE;
			}
			if (!self->use_24h && (c == 'p' || c == 'P') && self->hour < 12)
			{
				step_segment (self, LRG_TIME_PICKER_SEGMENT_PERIOD, 1);
				return TRUE;
			}
			return FALSE;
		}

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_time_picker_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgTimePicker *self = LRG_TIME_PICKER (object);

	switch (prop_id)
	{
	case PROP_HOUR:
		g_value_set_int (value, self->hour);
		break;
	case PROP_MINUTE:
		g_value_set_int (value, self->minute);
		break;
	case PROP_SECOND:
		g_value_set_int (value, self->second);
		break;
	case PROP_SHOW_SECONDS:
		g_value_set_boolean (value, self->show_seconds);
		break;
	case PROP_USE_24H:
		g_value_set_boolean (value, self->use_24h);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_time_picker_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgTimePicker *self = LRG_TIME_PICKER (object);

	switch (prop_id)
	{
	case PROP_HOUR:
		lrg_time_picker_set_hour (self, g_value_get_int (value));
		break;
	case PROP_MINUTE:
		lrg_time_picker_set_minute (self, g_value_get_int (value));
		break;
	case PROP_SECOND:
		lrg_time_picker_set_second (self, g_value_get_int (value));
		break;
	case PROP_SHOW_SECONDS:
		lrg_time_picker_set_show_seconds (self, g_value_get_boolean (value));
		break;
	case PROP_USE_24H:
		lrg_time_picker_set_use_24h (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_time_picker_class_init (LrgTimePickerClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->get_property = lrg_time_picker_get_property;
	object_class->set_property = lrg_time_picker_set_property;

	widget_class->style_defaults = lrg_time_picker_style_defaults;
	widget_class->measure = lrg_time_picker_measure;
	widget_class->paint = lrg_time_picker_paint;
	widget_class->step = lrg_time_picker_step;
	widget_class->handle_event = lrg_time_picker_handle_event;

	/**
	 * LrgTimePicker:hour:
	 *
	 * The hour, 0..23, whatever the display mode.
	 */
	properties[PROP_HOUR] =
		g_param_spec_int ("hour", NULL, NULL, 0, 23, 0,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimePicker:minute:
	 *
	 * The minute, 0..59.
	 */
	properties[PROP_MINUTE] =
		g_param_spec_int ("minute", NULL, NULL, 0, 59, 0,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimePicker:second:
	 *
	 * The second, 0..59.
	 */
	properties[PROP_SECOND] =
		g_param_spec_int ("second", NULL, NULL, 0, 59, 0,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimePicker:show-seconds:
	 *
	 * Whether the seconds field is shown.
	 */
	properties[PROP_SHOW_SECONDS] =
		g_param_spec_boolean ("show-seconds", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTimePicker:use-24h:
	 *
	 * %TRUE for a 24-hour display, %FALSE for 12-hour with AM / PM.
	 */
	properties[PROP_USE_24H] =
		g_param_spec_boolean ("use-24h", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgTimePicker::time-changed:
	 * @self: the time picker
	 *
	 * Emitted once whenever the hour, minute or second changes, by the
	 * user or from code.
	 */
	signals[SIGNAL_TIME_CHANGED] =
		g_signal_new ("time-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_time_picker_init (LrgTimePicker *self)
{
	gint s;

	self->use_24h = TRUE;
	self->hover = -1;
	self->typed = -1;
	self->drag_segment = -1;
	for (s = 0; s < N_SEGMENTS; s++)
	{
		lrg_spring_init (&self->hover_spring[s], 0.0f, LRG_MOTION_SMOOTH);
		lrg_spring_init (&self->roll[s], 0.0f, LRG_MOTION_SNAPPY);
	}

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTimePicker *
lrg_time_picker_new (void)
{
	return g_object_new (LRG_TYPE_TIME_PICKER, NULL);
}

gint
lrg_time_picker_get_hour (LrgTimePicker *self)
{
	g_return_val_if_fail (LRG_IS_TIME_PICKER (self), 0);

	return self->hour;
}

void
lrg_time_picker_set_hour (LrgTimePicker *self,
                          gint           hour)
{
	g_return_if_fail (LRG_IS_TIME_PICKER (self));

	apply_time (self, hour, self->minute, self->second, 0);
}

gint
lrg_time_picker_get_minute (LrgTimePicker *self)
{
	g_return_val_if_fail (LRG_IS_TIME_PICKER (self), 0);

	return self->minute;
}

void
lrg_time_picker_set_minute (LrgTimePicker *self,
                            gint           minute)
{
	g_return_if_fail (LRG_IS_TIME_PICKER (self));

	apply_time (self, self->hour, minute, self->second, 0);
}

gint
lrg_time_picker_get_second (LrgTimePicker *self)
{
	g_return_val_if_fail (LRG_IS_TIME_PICKER (self), 0);

	return self->second;
}

void
lrg_time_picker_set_second (LrgTimePicker *self,
                            gint           second)
{
	g_return_if_fail (LRG_IS_TIME_PICKER (self));

	apply_time (self, self->hour, self->minute, second, 0);
}

void
lrg_time_picker_set_time (LrgTimePicker *self,
                          gint           hour,
                          gint           minute,
                          gint           second)
{
	g_return_if_fail (LRG_IS_TIME_PICKER (self));

	apply_time (self, hour, minute, second, 0);
}

gboolean
lrg_time_picker_get_show_seconds (LrgTimePicker *self)
{
	g_return_val_if_fail (LRG_IS_TIME_PICKER (self), FALSE);

	return self->show_seconds;
}

void
lrg_time_picker_set_show_seconds (LrgTimePicker *self,
                                  gboolean       show_seconds)
{
	g_return_if_fail (LRG_IS_TIME_PICKER (self));

	show_seconds = !!show_seconds;
	if (show_seconds == self->show_seconds)
		return;

	self->show_seconds = show_seconds;
	if (!show_seconds && self->active == LRG_TIME_PICKER_SEGMENT_SECOND)
		self->active = LRG_TIME_PICKER_SEGMENT_MINUTE;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_SECONDS]);
}

gboolean
lrg_time_picker_get_use_24h (LrgTimePicker *self)
{
	g_return_val_if_fail (LRG_IS_TIME_PICKER (self), TRUE);

	return self->use_24h;
}

void
lrg_time_picker_set_use_24h (LrgTimePicker *self,
                             gboolean       use_24h)
{
	g_return_if_fail (LRG_IS_TIME_PICKER (self));

	use_24h = !!use_24h;
	if (use_24h == self->use_24h)
		return;

	self->use_24h = use_24h;
	if (use_24h && self->active == LRG_TIME_PICKER_SEGMENT_PERIOD)
		self->active = LRG_TIME_PICKER_SEGMENT_HOUR;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_USE_24H]);
}

gboolean
lrg_time_picker_get_is_pm (LrgTimePicker *self)
{
	g_return_val_if_fail (LRG_IS_TIME_PICKER (self), FALSE);

	return self->hour >= 12;
}

LrgTimePickerSegment
lrg_time_picker_get_active_segment (LrgTimePicker *self)
{
	g_return_val_if_fail (LRG_IS_TIME_PICKER (self), LRG_TIME_PICKER_SEGMENT_HOUR);

	return self->active;
}

void
lrg_time_picker_set_active_segment (LrgTimePicker        *self,
                                    LrgTimePickerSegment  segment)
{
	g_return_if_fail (LRG_IS_TIME_PICKER (self));

	if ((gint) segment < 0 || (gint) segment >= N_SEGMENTS || !segment_shown (self, segment))
		return;
	self->active = segment;
	self->typed = -1;
}

void
lrg_time_picker_step_segment (LrgTimePicker        *self,
                              LrgTimePickerSegment  segment,
                              gint                  delta)
{
	g_return_if_fail (LRG_IS_TIME_PICKER (self));

	step_segment (self, segment, delta);
}

gboolean
lrg_time_picker_get_segment_bounds (LrgTimePicker        *self,
                                    LrgTimePickerSegment  segment,
                                    GrlRectangle         *bounds)
{
	SegmentSlot slots[N_SEGMENTS];
	guint       n;
	guint       i;

	g_return_val_if_fail (LRG_IS_TIME_PICKER (self), FALSE);
	g_return_val_if_fail (bounds != NULL, FALSE);

	n = layout_segments (self, slots);
	for (i = 0; i < n; i++)
	{
		if (slots[i].segment != segment)
			continue;
		bounds->x = lrg_widget_get_world_x (LRG_WIDGET (self)) + slots[i].x;
		bounds->y = lrg_widget_get_world_y (LRG_WIDGET (self));
		bounds->width = slots[i].width;
		bounds->height = lrg_widget_get_height (LRG_WIDGET (self));
		return TRUE;
	}
	return FALSE;
}

gchar *
lrg_time_picker_get_text (LrgTimePicker *self)
{
	GString *s;

	g_return_val_if_fail (LRG_IS_TIME_PICKER (self), NULL);

	s = g_string_new (NULL);
	g_string_append_printf (s, "%02d:%02d", display_hour (self), self->minute);
	if (self->show_seconds)
		g_string_append_printf (s, ":%02d", self->second);
	if (!self->use_24h)
		g_string_append (s, self->hour >= 12 ? " PM" : " AM");
	return g_string_free (s, FALSE);
}
