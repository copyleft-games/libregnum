/* lrg-spin-box.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpinBox: A numeric field with step buttons and drag-to-scrub.
 *
 * Layout: an INPUT-coloured box (BOX part) with the formatted value
 * (LABEL part) centred and two small rounded buttons (ARROW part) at
 * the ends, minus on the left and plus on the right. Each button has
 * its own hover and press springs, stepped by step() with the canvas
 * frame time, so its
 * look glides between the resting, hovered and pressed variants and it
 * squashes slightly when pressed. Holding a button repeats after
 * 0.4 s at 12 Hz; dragging sideways on the value scrubs one step per
 * 6 units; every change flashes the border in the accent colour.
 *
 * Press gestures: a press on the value (scrubbing) or on a step button
 * (a click or a held repeat) is one gesture, announced with
 * LrgSpinBox::scrub-begin before the first change and closed with
 * LrgSpinBox::scrub-end on release (commit), on focus loss (commit) or
 * on CANCEL (Escape, gamepad B), which also puts back the value the
 * gesture started from. Editors that record undo steps group the
 * value changes in between into one step.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-spin-box.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Units of horizontal drag per step while scrubbing */
#define SCRUB_UNITS (6.0f)

/* Auto-repeat: delay before the first repeat, then the rate */
#define REPEAT_DELAY (0.4)
#define REPEAT_HZ       (12.0)

/* Space between the box edge and the buttons */
#define BUTTON_INSET (4.0f)

typedef enum
{
	SPIN_PART_NONE,
	SPIN_PART_MINUS,
	SPIN_PART_PLUS,
	SPIN_PART_VALUE
} SpinPart;

struct _LrgSpinBox
{
	LrgWidget  parent_instance;

	gdouble    value;
	gdouble    min;
	gdouble    max;
	gdouble    step;
	guint      digits;

	/* Pointer state */
	SpinPart   hover_part;
	SpinPart   press_part;
	gboolean   scrubbing;
	gfloat     scrub_x;
	gdouble    scrub_value;
	gdouble    held;        /* seconds a step button has been held */
	gboolean   hold_timing; /* held counts from the frame after the press */
	guint      repeats;

	/* Press gesture (scrub-begin .. scrub-end) and the value it started
	 * from, put back when the gesture is cancelled */
	gboolean   gesture;
	gdouble    gesture_value;

	/* Own springs, stepped in step(); until the first step the flash
	 * is ignored so a new box does not flash in */
	LrgSpring  hover_minus;
	LrgSpring  hover_plus;
	LrgSpring  press_minus;
	LrgSpring  press_plus;
	LrgSpring  flash;
	gboolean   stepped;
};

G_DEFINE_FINAL_TYPE (LrgSpinBox, lrg_spin_box, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_VALUE,
	PROP_MIN,
	PROP_MAX,
	PROP_STEP,
	PROP_DIGITS,
	N_PROPS
};

enum
{
	SIGNAL_VALUE_CHANGED,
	SIGNAL_SCRUB_BEGIN,
	SIGNAL_SCRUB_END,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* format_value: @value with the configured number of decimals */
static gchar *
format_value (LrgSpinBox *self,
              gdouble     value)
{
	return g_strdup_printf ("%.*f", (gint) self->digits, value);
}

/* tidy: drop floating-point dust (0.1 + 0.2) left by repeated steps */
static gdouble
tidy (gdouble v)
{
	return round (v * 1e9) / 1e9;
}

/*
 * button_rects:
 *
 * World rectangles of the minus and plus buttons: squares inset from
 * the box, at its left and right ends.
 */
static void
button_rects (LrgSpinBox   *self,
              GrlRectangle *minus,
              GrlRectangle *plus)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     x = lrg_widget_get_world_x (w);
	gfloat     y = lrg_widget_get_world_y (w);
	gfloat     width = lrg_widget_get_width (w);
	gfloat     size = MAX (0.0f, lrg_widget_get_height (w) - BUTTON_INSET * 2.0f);

	size = MIN (size, MAX (0.0f, width * 0.5f - BUTTON_INSET * 2.0f));

	minus->x = x + BUTTON_INSET;
	minus->y = y + (lrg_widget_get_height (w) - size) * 0.5f;
	minus->width = size;
	minus->height = size;

	*plus = *minus;
	plus->x = x + width - BUTTON_INSET - size;
}

static gboolean
rect_has (const GrlRectangle *r,
          gfloat              px,
          gfloat              py)
{
	return px >= r->x && px < r->x + r->width && py >= r->y && py < r->y + r->height;
}

/* part_at: which part of the spin box is under a world point */
static SpinPart
part_at (LrgSpinBox *self,
         gfloat      px,
         gfloat      py)
{
	GrlRectangle minus;
	GrlRectangle plus;

	if (!lrg_widget_contains_point (LRG_WIDGET (self), px, py))
		return SPIN_PART_NONE;

	button_rects (self, &minus, &plus);
	if (rect_has (&minus, px, py))
		return SPIN_PART_MINUS;
	if (rect_has (&plus, px, py))
		return SPIN_PART_PLUS;
	return SPIN_PART_VALUE;
}

/* ==========================================================================
 * Layout and looks
 * ========================================================================== */

static void
lrg_spin_box_measure (LrgWidget *widget,
                      gfloat    *preferred_width,
                      gfloat    *preferred_height)
{
	LrgSpinBox     *self = LRG_SPIN_BOX (widget);
	LrgTheme       *theme = lrg_widget_get_theme (widget);
	gfloat          size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat          height = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT);
	gfloat          pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	g_autofree gchar *lo = format_value (self, self->min);
	g_autofree gchar *hi = format_value (self, self->max);
	gfloat          wl = 0.0f;
	gfloat          wh = 0.0f;
	gfloat          button = height - BUTTON_INSET * 2.0f;

	/* Room for the widest value plus a button and padding each side */
	lrg_paint_measure_text (lrg_theme_get_ui_font (theme), lo, size, 1.0f, &wl, NULL);
	lrg_paint_measure_text (lrg_theme_get_ui_font (theme), hi, size, 1.0f, &wh, NULL);

	*preferred_width = ceilf (MAX (wl, wh)) + (button + BUTTON_INSET) * 2.0f + pad * 2.0f;
	*preferred_height = height;
}

static void
lrg_spin_box_style_defaults (LrgWidget      *widget,
                             LrgUIPart       part,
                             LrgWidgetState  state,
                             LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gfloat    border = lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH);
	GrlColor  c;

	switch (part)
	{
	case LRG_UI_PART_BOX:
		/* Entry-like well; the border firms up on hover and turns
		 * accent while focused */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
		if (state & LRG_WIDGET_STATE_FOCUSED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
		else if (state & LRG_WIDGET_STATE_HOVERED)
			lrg_paint_color_mix (lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
			                     lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 0.5f, &c);
		else
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER);
		lrg_part_look_set_border (look, &c, border);
		lrg_part_look_set_radius (look, radius);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_ARROW:
		/* Step buttons: control colours, glyph in text colour */
		if (state & LRG_WIDGET_STATE_PRESSED)
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED));
		else if (state & LRG_WIDGET_STATE_HOVERED)
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER));
		else
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL));
		if (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED))
			lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		else
			lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_radius (look, MAX (2.0f, radius - BUTTON_INSET * 0.5f));
		lrg_part_look_set_size (look, 2.0f);
		return;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_DISABLED) ? LRG_THEME_COLOR_DISABLED
		                                                                  : LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE));
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_spin_box_parent_class)->style_defaults (widget, part, state, look);
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/* holding_button: a step button is pressed */
static gboolean
holding_button (LrgSpinBox *self)
{
	return self->press_part == SPIN_PART_MINUS || self->press_part == SPIN_PART_PLUS;
}

/*
 * lrg_spin_box_step:
 *
 * Advances the per-button springs and the flash by the canvas frame
 * time, and runs the press-and-hold auto-repeat from the time the
 * button has been held (also on the canvas clock, so repeats are
 * deterministic). The first step snaps everything to rest.
 *
 * Returns: %TRUE while a spring moves or a step button is held (the
 *   repeat needs frames)
 */
static gboolean
lrg_spin_box_step (LrgWidget *widget,
                   gfloat     delta_time)
{
	LrgSpinBox *self = LRG_SPIN_BOX (widget);
	LrgMotion   motion = lrg_widget_get_motion (widget);
	gboolean    busy = FALSE;

	/* 1. Springs follow the pointer state */
	lrg_spring_set_motion (&self->hover_minus, motion);
	lrg_spring_set_motion (&self->hover_plus, motion);
	lrg_spring_set_target (&self->hover_minus, self->hover_part == SPIN_PART_MINUS ? 1.0f : 0.0f);
	lrg_spring_set_target (&self->hover_plus, self->hover_part == SPIN_PART_PLUS ? 1.0f : 0.0f);
	lrg_spring_set_target (&self->press_minus,
	                       self->press_part == SPIN_PART_MINUS && self->hover_part == SPIN_PART_MINUS
	                       ? 1.0f : 0.0f);
	lrg_spring_set_target (&self->press_plus,
	                       self->press_part == SPIN_PART_PLUS && self->hover_part == SPIN_PART_PLUS
	                       ? 1.0f : 0.0f);
	if (!self->stepped)
	{
		lrg_spring_snap (&self->hover_minus, self->hover_minus.target);
		lrg_spring_snap (&self->hover_plus, self->hover_plus.target);
		lrg_spring_snap (&self->press_minus, self->press_minus.target);
		lrg_spring_snap (&self->press_plus, self->press_plus.target);
		lrg_spring_snap (&self->flash, 0.0f);
		self->stepped = TRUE;
	}
	busy |= lrg_spring_step (&self->hover_minus, delta_time);
	busy |= lrg_spring_step (&self->hover_plus, delta_time);
	busy |= lrg_spring_step (&self->press_minus, delta_time);
	busy |= lrg_spring_step (&self->press_plus, delta_time);
	busy |= lrg_spring_step (&self->flash, delta_time);

	/* 2. Hold to repeat, while the pointer stays on the held button.
	 *    The frame of the press itself does not count: its delta time
	 *    mostly passed before the press. */
	if (!holding_button (self))
		return busy;

	if (self->hold_timing)
		self->held += (gdouble) MAX (delta_time, 0.0f);
	self->hold_timing = TRUE;

	if (self->hover_part == self->press_part && self->held > REPEAT_DELAY)
	{
		guint want = (guint) ((self->held - REPEAT_DELAY) * REPEAT_HZ) + 1;

		while (self->repeats < want)
		{
			lrg_spin_box_spin (self, self->press_part == SPIN_PART_MINUS ? -1 : 1);
			self->repeats++;
		}
	}
	return TRUE;
}

/*
 * paint_button:
 *
 * One step button: its look blended by its own hover / press springs,
 * squashed a little while pressed, with a minus or plus glyph.
 */
static void
paint_button (LrgSpinBox         *self,
              LrgPaintList       *list,
              const GrlRectangle *r,
              gboolean            plus,
              gfloat              hover,
              gfloat              press)
{
	LrgWidget     *w = LRG_WIDGET (self);
	LrgWidgetState base = lrg_widget_get_state_flags (w) &
	                      ~(LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED);
	LrgPartLook    look;
	LrgPartLook    variant;
	gfloat         cx = r->x + r->width * 0.5f;
	gfloat         cy = r->y + r->height * 0.5f;
	gfloat         arm = MAX (3.0f, r->width * 0.2f);
	guint          mark;
	gboolean       at_limit;

	if (r->width <= 0.0f)
		return;

	lrg_widget_resolve_look_for_state (w, LRG_UI_PART_ARROW, base, &look);
	if (hover > 0.001f)
	{
		lrg_widget_resolve_look_for_state (w, LRG_UI_PART_ARROW, base | LRG_WIDGET_STATE_HOVERED, &variant);
		lrg_part_look_lerp (&look, &variant, CLAMP (hover, 0.0f, 1.0f), &look);
	}
	if (press > 0.001f)
	{
		lrg_widget_resolve_look_for_state (w, LRG_UI_PART_ARROW,
		                                   base | LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED,
		                                   &variant);
		lrg_part_look_lerp (&look, &variant, CLAMP (press, 0.0f, 1.0f), &look);
	}

	/* A button that cannot step further is dimmed */
	at_limit = plus ? self->value >= self->max : self->value <= self->min;
	if (at_limit)
		lrg_paint_list_push_opacity (list, 0.4f);

	mark = lrg_paint_list_begin_group (list);
	lrg_widget_paint_box (w, list, &look, r->x, r->y, r->width, r->height);
	lrg_paint_list_line (list, cx - arm, cy, cx + arm, cy, look.size, &look.foreground);
	if (plus)
		lrg_paint_list_line (list, cx, cy - arm, cx, cy + arm, look.size, &look.foreground);
	if (press > 0.001f)
		lrg_paint_list_transform_group (list, mark, 1.0f - 0.1f * CLAMP (press, 0.0f, 1.0f),
		                                cx, cy, 0.0f, 0.0f, 1.0f);

	if (at_limit)
		lrg_paint_list_pop_opacity (list);
}

static void
lrg_spin_box_paint (LrgWidget    *widget,
                    LrgPaintList *list)
{
	LrgSpinBox       *self = LRG_SPIN_BOX (widget);
	LrgTheme         *theme = lrg_widget_get_theme (widget);
	LrgPartLook       box;
	LrgPartLook       label;
	GrlRectangle      minus;
	GrlRectangle      plus;
	g_autofree gchar *text = NULL;
	gfloat            x = lrg_widget_get_world_x (widget);
	gfloat            y = lrg_widget_get_world_y (widget);
	gfloat            w = lrg_widget_get_width (widget);
	gfloat            h = lrg_widget_get_height (widget);
	gfloat            tw = 0.0f;
	gfloat            th = 0.0f;
	gfloat            flash;
	gboolean          faded;

	/* Body, with the border flashing towards the accent on change */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	flash = self->stepped ? CLAMP (self->flash.value, 0.0f, 1.0f) : 0.0f;
	if (flash > 0.001f)
	{
		lrg_paint_color_mix (&box.border, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), flash,
		                     &box.border);
		box.border_width = MAX (box.border_width, 1.0f + flash);
	}
	/* The body's opacity (disabled) fades the whole widget */
	faded = box.opacity < 1.0f;
	if (faded)
		lrg_paint_list_push_opacity (list, box.opacity);
	box.opacity = 1.0f;
	lrg_widget_paint_box (widget, list, &box, x, y, w, h);

	/* Buttons */
	button_rects (self, &minus, &plus);
	paint_button (self, list, &minus, FALSE, self->hover_minus.value, self->press_minus.value);
	paint_button (self, list, &plus, TRUE, self->hover_plus.value, self->press_plus.value);

	/* Value, centred between the buttons; accent while scrubbing */
	lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
	if (self->scrubbing)
		label.foreground = *lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	text = format_value (self, self->value);
	lrg_paint_measure_text (lrg_theme_get_ui_font (theme), text, label.size, 1.0f, &tw, &th);
	lrg_paint_list_push_clip (list, minus.x + minus.width, y, MAX (0.0f, plus.x - minus.x - minus.width), h);
	lrg_paint_list_text (list, lrg_theme_get_ui_font (theme), text,
	                     roundf (x + (w - tw) * 0.5f), roundf (y + (h - th) * 0.5f),
	                     label.size, 1.0f, &label.foreground);
	lrg_paint_list_pop_clip (list);

	if (faded)
		lrg_paint_list_pop_opacity (list);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* end_press: forget the pressed button / scrub */
static void
end_press (LrgSpinBox *self)
{
	self->press_part = SPIN_PART_NONE;
	self->scrubbing = FALSE;
	self->repeats = 0;
}

/*
 * begin_gesture:
 *
 * Opens a press gesture: remembers the value to restore on cancel and
 * emits scrub-begin. Called before the press changes anything, so the
 * first step of a step button already belongs to the gesture.
 */
static void
begin_gesture (LrgSpinBox *self)
{
	if (self->gesture)
		return;
	self->gesture = TRUE;
	self->gesture_value = self->value;
	g_signal_emit (self, signals[SIGNAL_SCRUB_BEGIN], 0);
}

/*
 * end_gesture:
 *
 * Closes the press gesture, if one is open. Without @commit the value
 * goes back to where the gesture started (emitting value-changed when
 * that differs) before scrub-end is emitted, so a handler of scrub-end
 * already sees the restored value.
 */
static void
end_gesture (LrgSpinBox *self,
             gboolean    commit)
{
	if (!self->gesture)
		return;
	self->gesture = FALSE;
	if (!commit)
		lrg_spin_box_set_value (self, self->gesture_value);
	g_signal_emit (self, signals[SIGNAL_SCRUB_END], 0, commit);
}

static gboolean
lrg_spin_box_handle_event (LrgWidget        *widget,
                           const LrgUIEvent *event)
{
	LrgSpinBox *self = LRG_SPIN_BOX (widget);
	gfloat      px = lrg_ui_event_get_x (event);
	gfloat      py = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover_part = part_at (self, px, py);
		if (self->scrubbing)
		{
			/* One step per SCRUB_UNITS of horizontal travel */
			gint steps = (gint) truncf ((px - self->scrub_x) / SCRUB_UNITS);

			lrg_spin_box_set_value (self, tidy (self->scrub_value + (gdouble) steps * self->step));
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_part = SPIN_PART_NONE;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		self->hover_part = part_at (self, px, py);
		end_press (self);
		end_gesture (self, TRUE);
		self->press_part = self->hover_part;
		self->held = 0.0;
		self->hold_timing = FALSE;

		/* A press on any part is one gesture, until release or cancel */
		if (self->press_part != SPIN_PART_NONE)
			begin_gesture (self);

		if (self->press_part == SPIN_PART_MINUS)
			lrg_spin_box_spin (self, -1);
		else if (self->press_part == SPIN_PART_PLUS)
			lrg_spin_box_spin (self, 1);
		else if (self->press_part == SPIN_PART_VALUE)
		{
			self->scrubbing = TRUE;
			self->scrub_x = px;
			self->scrub_value = self->value;
		}
		return self->press_part != SPIN_PART_NONE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0 || self->press_part == SPIN_PART_NONE)
			return FALSE;
		end_press (self);
		end_gesture (self, TRUE);
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		/* Escape in the middle of a press: restore and drop the rest
		 * of the drag (moves no longer scrub, the release is ignored) */
		if (!self->gesture)
			return FALSE;
		end_press (self);
		end_gesture (self, FALSE);
		return TRUE;

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat dy = lrg_ui_event_get_scroll_y (event);

			if (dy == 0.0f)
				return FALSE;
			lrg_spin_box_spin (self, dy > 0.0f ? 1 : -1);
			return TRUE;
		}

	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_UP:
			lrg_spin_box_spin (self, 1);
			return TRUE;
		case GRL_KEY_DOWN:
			lrg_spin_box_spin (self, -1);
			return TRUE;
		case GRL_KEY_PAGE_UP:
			lrg_spin_box_spin (self, 10);
			return TRUE;
		case GRL_KEY_PAGE_DOWN:
			lrg_spin_box_spin (self, -10);
			return TRUE;
		case GRL_KEY_HOME:
			lrg_spin_box_set_value (self, self->min);
			return TRUE;
		case GRL_KEY_END:
			lrg_spin_box_set_value (self, self->max);
			return TRUE;
		default:
			return FALSE;
		}

	case LRG_UI_EVENT_FOCUS_OUT:
		end_press (self);
		end_gesture (self, TRUE);
		return FALSE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_spin_box_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgSpinBox *self = LRG_SPIN_BOX (object);

	switch (prop_id)
	{
	case PROP_VALUE:
		g_value_set_double (value, self->value);
		break;
	case PROP_MIN:
		g_value_set_double (value, self->min);
		break;
	case PROP_MAX:
		g_value_set_double (value, self->max);
		break;
	case PROP_STEP:
		g_value_set_double (value, self->step);
		break;
	case PROP_DIGITS:
		g_value_set_uint (value, self->digits);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_spin_box_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgSpinBox *self = LRG_SPIN_BOX (object);

	switch (prop_id)
	{
	case PROP_VALUE:
		lrg_spin_box_set_value (self, g_value_get_double (value));
		break;
	case PROP_MIN:
		lrg_spin_box_set_min (self, g_value_get_double (value));
		break;
	case PROP_MAX:
		lrg_spin_box_set_max (self, g_value_get_double (value));
		break;
	case PROP_STEP:
		lrg_spin_box_set_step (self, g_value_get_double (value));
		break;
	case PROP_DIGITS:
		lrg_spin_box_set_digits (self, g_value_get_uint (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_spin_box_class_init (LrgSpinBoxClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->get_property = lrg_spin_box_get_property;
	object_class->set_property = lrg_spin_box_set_property;

	widget_class->measure = lrg_spin_box_measure;
	widget_class->paint = lrg_spin_box_paint;
	widget_class->step = lrg_spin_box_step;
	widget_class->style_defaults = lrg_spin_box_style_defaults;
	widget_class->handle_event = lrg_spin_box_handle_event;

	/**
	 * LrgSpinBox:value:
	 *
	 * The current value, always within [min, max].
	 */
	properties[PROP_VALUE] =
		g_param_spec_double ("value", "Value", "The current value",
		                     -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSpinBox:min:
	 *
	 * The smallest value.
	 */
	properties[PROP_MIN] =
		g_param_spec_double ("min", "Minimum", "The smallest value",
		                     -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSpinBox:max:
	 *
	 * The largest value.
	 */
	properties[PROP_MAX] =
		g_param_spec_double ("max", "Maximum", "The largest value",
		                     -G_MAXDOUBLE, G_MAXDOUBLE, 100.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSpinBox:step:
	 *
	 * Amount one click, arrow key, wheel notch or 6 units of scrubbing
	 * changes the value. Page Up / Page Down move ten steps.
	 */
	properties[PROP_STEP] =
		g_param_spec_double ("step", "Step", "The step increment",
		                     0.0, G_MAXDOUBLE, 1.0,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgSpinBox:digits:
	 *
	 * Number of decimals shown.
	 */
	properties[PROP_DIGITS] =
		g_param_spec_uint ("digits", "Digits", "Number of decimals shown",
		                   0, 10, 0,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgSpinBox::value-changed:
	 * @self: the spin box
	 *
	 * Emitted whenever the value changes, by the user or the program.
	 */
	signals[SIGNAL_VALUE_CHANGED] =
		g_signal_new ("value-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgSpinBox::scrub-begin:
	 * @self: the spin box
	 *
	 * A press gesture starts: the pointer went down on the value
	 * (scrubbing) or on a step button (a click or a held repeat).
	 * Emitted before the press changes the value, so every
	 * #LrgSpinBox::value-changed until #LrgSpinBox::scrub-end belongs
	 * to the gesture. Wheel notches and keys are not gestures.
	 */
	signals[SIGNAL_SCRUB_BEGIN] =
		g_signal_new ("scrub-begin",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgSpinBox::scrub-end:
	 * @self: the spin box
	 * @commit: %TRUE when the gesture ended normally (release, focus
	 *   lost), %FALSE when it was cancelled (Escape, gamepad B,
	 *   lrg_spin_box_cancel_scrub()); the value is then already back
	 *   at what it was at #LrgSpinBox::scrub-begin
	 *
	 * The press gesture ended.
	 */
	signals[SIGNAL_SCRUB_END] =
		g_signal_new ("scrub-end",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
lrg_spin_box_init (LrgSpinBox *self)
{
	self->value = 0.0;
	self->min = 0.0;
	self->max = 100.0;
	self->step = 1.0;
	self->digits = 0;

	lrg_spring_init (&self->hover_minus, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&self->hover_plus, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&self->press_minus, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->press_plus, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init_full (&self->flash, 0.0f, 0.5f, 1.0f);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgSpinBox *
lrg_spin_box_new (void)
{
	return g_object_new (LRG_TYPE_SPIN_BOX, NULL);
}

LrgSpinBox *
lrg_spin_box_new_with_range (gdouble min,
                             gdouble max,
                             gdouble step)
{
	g_return_val_if_fail (min <= max, NULL);

	return g_object_new (LRG_TYPE_SPIN_BOX,
	                     "max", max,
	                     "min", min,
	                     "step", step,
	                     "value", min,
	                     NULL);
}

gdouble
lrg_spin_box_get_value (LrgSpinBox *self)
{
	g_return_val_if_fail (LRG_IS_SPIN_BOX (self), 0.0);
	return self->value;
}

void
lrg_spin_box_set_value (LrgSpinBox *self,
                        gdouble     value)
{
	g_return_if_fail (LRG_IS_SPIN_BOX (self));

	value = CLAMP (value, self->min, MAX (self->min, self->max));
	if (value == self->value)
		return;

	self->value = value;

	/* Flash the border: jump to full accent, fade back */
	lrg_spring_snap (&self->flash, 1.0f);
	lrg_spring_set_target (&self->flash, 0.0f);

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VALUE]);
	g_signal_emit (self, signals[SIGNAL_VALUE_CHANGED], 0);
}

gdouble
lrg_spin_box_get_min (LrgSpinBox *self)
{
	g_return_val_if_fail (LRG_IS_SPIN_BOX (self), 0.0);
	return self->min;
}

void
lrg_spin_box_set_min (LrgSpinBox *self,
                      gdouble     min)
{
	g_return_if_fail (LRG_IS_SPIN_BOX (self));

	if (self->min == min)
		return;
	self->min = min;
	if (self->max < min)
	{
		self->max = min;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX]);
	}
	lrg_spin_box_set_value (self, self->value);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN]);
}

gdouble
lrg_spin_box_get_max (LrgSpinBox *self)
{
	g_return_val_if_fail (LRG_IS_SPIN_BOX (self), 0.0);
	return self->max;
}

void
lrg_spin_box_set_max (LrgSpinBox *self,
                      gdouble     max)
{
	g_return_if_fail (LRG_IS_SPIN_BOX (self));

	if (self->max == max)
		return;
	self->max = max;
	if (self->min > max)
	{
		self->min = max;
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN]);
	}
	lrg_spin_box_set_value (self, self->value);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX]);
}

void
lrg_spin_box_set_range (LrgSpinBox *self,
                        gdouble     min,
                        gdouble     max)
{
	g_return_if_fail (LRG_IS_SPIN_BOX (self));
	g_return_if_fail (min <= max);

	g_object_freeze_notify (G_OBJECT (self));
	if (min > self->max)
	{
		lrg_spin_box_set_max (self, max);
		lrg_spin_box_set_min (self, min);
	}
	else
	{
		lrg_spin_box_set_min (self, min);
		lrg_spin_box_set_max (self, max);
	}
	g_object_thaw_notify (G_OBJECT (self));
}

gdouble
lrg_spin_box_get_step (LrgSpinBox *self)
{
	g_return_val_if_fail (LRG_IS_SPIN_BOX (self), 0.0);
	return self->step;
}

void
lrg_spin_box_set_step (LrgSpinBox *self,
                       gdouble     step)
{
	g_return_if_fail (LRG_IS_SPIN_BOX (self));
	g_return_if_fail (step >= 0.0);

	if (self->step == step)
		return;
	self->step = step;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STEP]);
}

guint
lrg_spin_box_get_digits (LrgSpinBox *self)
{
	g_return_val_if_fail (LRG_IS_SPIN_BOX (self), 0);
	return self->digits;
}

void
lrg_spin_box_set_digits (LrgSpinBox *self,
                         guint       digits)
{
	g_return_if_fail (LRG_IS_SPIN_BOX (self));

	digits = MIN (digits, 10);
	if (self->digits == digits)
		return;
	self->digits = digits;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIGITS]);
}

gchar *
lrg_spin_box_get_text (LrgSpinBox *self)
{
	g_return_val_if_fail (LRG_IS_SPIN_BOX (self), NULL);
	return format_value (self, self->value);
}

void
lrg_spin_box_spin (LrgSpinBox *self,
                   gint        steps)
{
	g_return_if_fail (LRG_IS_SPIN_BOX (self));

	lrg_spin_box_set_value (self, tidy (self->value + (gdouble) steps * self->step));
}

gboolean
lrg_spin_box_get_scrubbing (LrgSpinBox *self)
{
	g_return_val_if_fail (LRG_IS_SPIN_BOX (self), FALSE);
	return self->gesture;
}

void
lrg_spin_box_cancel_scrub (LrgSpinBox *self)
{
	g_return_if_fail (LRG_IS_SPIN_BOX (self));

	if (!self->gesture)
		return;
	end_press (self);
	end_gesture (self, FALSE);
}
