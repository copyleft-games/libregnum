/* lrg-date-picker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDatePicker: A date field that opens a month calendar popup.
 *
 * Closed, the picker is an input-like field (BOX part) showing the date
 * formatted with g_date_time_format() and a small calendar icon (ARROW
 * part). Clicking it or activating it opens a private calendar popup
 * (LrgDateCalendar) on the canvas popup layer: a month title between
 * previous / next buttons, a weekday header and a 6 x 7 day grid. A
 * soft highlight bubble glides (springs) to the day under the pointer
 * or the keyboard cursor, the selected day is an ACCENT disc, today has
 * a ring, and turning the month slides the grid in from the side.
 *
 * Dates are handled as #GDate day numbers internally; the public API
 * speaks #GDateTime at local midnight. Weeks start on Monday and the
 * weekday / month names come from the C locale via g_date_time_format().
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-date-picker.h"
#include "lrg-canvas.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

#define DEFAULT_FORMAT "%Y-%m-%d"
#define DEFAULT_PLACEHOLDER "Select a date"

/* Calendar inset */
#define CAL_PAD (8.0f)

/* Grid size */
#define CAL_COLS (7)
#define CAL_ROWS (6)
#define CAL_CELLS (CAL_COLS * CAL_ROWS)

/* Header buttons */
#define BUTTON_NONE (0)
#define BUTTON_PREV (1)
#define BUTTON_NEXT (2)

#define LRG_TYPE_DATE_CALENDAR (lrg_date_calendar_get_type ())
G_DECLARE_FINAL_TYPE (LrgDateCalendar, lrg_date_calendar, LRG, DATE_CALENDAR, LrgWidget)

struct _LrgDatePicker
{
	LrgWidget        parent_instance;

	GDateTime       *date;
	GDateTime       *min_date;
	GDateTime       *max_date;
	GDateTime       *today;
	gchar           *format;
	gchar           *placeholder;

	/* Month on show in the calendar (0 = not chosen yet) */
	gint             view_year;
	gint             view_month;

	LrgDateCalendar *popup;
	LrgCanvas       *canvas;     /* weak */
	gulong           dismissed_id;
	gboolean         swallow_press;
};

struct _LrgDateCalendar
{
	LrgWidget      parent_instance;

	LrgDatePicker *picker;       /* weak */
	GDate          cursor;
	GDate          today;

	gint           hover_cell;   /* -1 = none */
	gint           hover_button;
	gint           press_cell;
	gint           press_button;
	gboolean       keyboard;     /* show the cursor bubble */

	/* Highlight bubble (cell units), its opacity, month slide, buttons */
	LrgSpring      hl_x;
	LrgSpring      hl_y;
	LrgSpring      hl_alpha;
	LrgSpring      slide;
	LrgSpring      button_hover[2];
	gboolean       stepped;
};

G_DEFINE_FINAL_TYPE (LrgDatePicker, lrg_date_picker, LRG_TYPE_WIDGET)
G_DEFINE_FINAL_TYPE (LrgDateCalendar, lrg_date_calendar, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_DATE,
	PROP_FORMAT,
	PROP_PLACEHOLDER,
	PROP_MIN_DATE,
	PROP_MAX_DATE,
	PROP_TODAY,
	PROP_POPUP_OPEN,
	N_PROPS
};

enum
{
	SIGNAL_DATE_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void picker_select_day (LrgDatePicker *self,
                               const GDate   *day,
                               gboolean       keyboard);

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgCanvas *
find_canvas (LrgWidget *widget)
{
	LrgWidget *root = lrg_widget_get_root (widget);

	return LRG_IS_CANVAS (root) ? LRG_CANVAS (root) : NULL;
}

static GrlColor
transparent (const GrlColor *color)
{
	GrlColor c = *color;

	c.a = 0;
	return c;
}

static gfloat
metric (LrgWidget      *widget,
        LrgThemeMetric  m)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (widget), m);
}

static const GrlColor *
color (LrgWidget     *widget,
       LrgThemeColor  c)
{
	return lrg_theme_get_color (lrg_widget_get_theme (widget), c);
}

static gfloat
text_width (LrgWidget   *widget,
            const gchar *text,
            gfloat       size)
{
	gfloat w = 0.0f;

	if (text == NULL || text[0] == '\0')
		return 0.0f;
	lrg_paint_measure_text (lrg_theme_get_ui_font (lrg_widget_get_theme (widget)), text, size, 1.0f, &w, NULL);
	return w;
}

/* paint_text_centered: @text centred on (cx, cy) */
static void
paint_text_centered (LrgPaintList   *list,
                     LrgWidget      *widget,
                     const gchar    *text,
                     gfloat          cx,
                     gfloat          cy,
                     gfloat          size,
                     const GrlColor *c)
{
	GrlFont *font = lrg_theme_get_ui_font (lrg_widget_get_theme (widget));
	gfloat   w = 0.0f;
	gfloat   h = size;

	if (text == NULL || text[0] == '\0')
		return;
	lrg_paint_measure_text (font, text, size, 1.0f, &w, &h);
	lrg_paint_list_text (list, font, text, roundf (cx - w * 0.5f), roundf (cy - h * 0.5f), size, 1.0f, c);
}

/* gdate_from_datetime: the day of @dt (in its own time zone) */
static void
gdate_from_datetime (GDateTime *dt,
                     GDate     *out)
{
	gint y;
	gint m;
	gint d;

	g_date_time_get_ymd (dt, &y, &m, &d);
	g_date_clear (out, 1);
	g_date_set_dmy (out, (GDateDay) d, (GDateMonth) m, (GDateYear) y);
}

/* datetime_from_gdate: local midnight of @day */
static GDateTime *
datetime_from_gdate (const GDate *day)
{
	return g_date_time_new_local (g_date_get_year (day), g_date_get_month (day), g_date_get_day (day),
	                              0, 0, 0.0);
}

/* date_only: local midnight of the day of @dt, or NULL */
static GDateTime *
date_only (GDateTime *dt)
{
	GDate d;

	if (dt == NULL)
		return NULL;
	gdate_from_datetime (dt, &d);
	return datetime_from_gdate (&d);
}

/* same_day: both NULL, or both the same day */
static gboolean
same_day (GDateTime *a,
          GDateTime *b)
{
	gint ay;
	gint am;
	gint ad;
	gint by;
	gint bm;
	gint bd;

	if (a == NULL || b == NULL)
		return a == b;
	g_date_time_get_ymd (a, &ay, &am, &ad);
	g_date_time_get_ymd (b, &by, &bm, &bd);
	return ay == by && am == bm && ad == bd;
}

/* day_enabled: inside min .. max */
static gboolean
day_enabled (LrgDatePicker *self,
             const GDate   *day)
{
	GDate lim;

	if (self->min_date != NULL)
	{
		gdate_from_datetime (self->min_date, &lim);
		if (g_date_compare (day, &lim) < 0)
			return FALSE;
	}
	if (self->max_date != NULL)
	{
		gdate_from_datetime (self->max_date, &lim);
		if (g_date_compare (day, &lim) > 0)
			return FALSE;
	}
	return TRUE;
}

/* clamp_day: move @day into min .. max */
static void
clamp_day (LrgDatePicker *self,
           GDate         *day)
{
	GDate lim;

	if (self->min_date != NULL)
	{
		gdate_from_datetime (self->min_date, &lim);
		if (g_date_compare (day, &lim) < 0)
			*day = lim;
	}
	if (self->max_date != NULL)
	{
		gdate_from_datetime (self->max_date, &lim);
		if (g_date_compare (day, &lim) > 0)
			*day = lim;
	}
}

/* today_gdate: the explicit today, or the system date */
static void
today_gdate (LrgDatePicker *self,
             GDate         *out)
{
	if (self->today != NULL)
		gdate_from_datetime (self->today, out);
	else
	{
		g_autoptr(GDateTime) now = g_date_time_new_now_local ();

		gdate_from_datetime (now, out);
	}
}

/* ensure_view: pick the month to show if none was chosen yet */
static void
ensure_view (LrgDatePicker *self)
{
	GDate d;

	if (self->view_year != 0)
		return;
	if (self->date != NULL)
		gdate_from_datetime (self->date, &d);
	else
	{
		today_gdate (self, &d);
		clamp_day (self, &d);
	}
	self->view_year = g_date_get_year (&d);
	self->view_month = g_date_get_month (&d);
}

/* grid_first: the Monday on or before the 1st of the shown month */
static void
grid_first (LrgDatePicker *self,
            GDate         *out)
{
	ensure_view (self);
	g_date_clear (out, 1);
	g_date_set_dmy (out, 1, (GDateMonth) self->view_month, (GDateYear) self->view_year);
	g_date_subtract_days (out, (guint) g_date_get_weekday (out) - 1);
}

/* cell_of: index of @day in the shown grid, or -1 */
static gint
cell_of (LrgDatePicker *self,
         const GDate   *day)
{
	GDate first;
	gint  n;

	grid_first (self, &first);
	n = g_date_days_between (&first, day);
	return (n >= 0 && n < CAL_CELLS) ? n : -1;
}

/* cell_day: the day shown in cell @i */
static void
cell_day (LrgDatePicker *self,
          gint           i,
          GDate         *out)
{
	grid_first (self, out);
	g_date_add_days (out, (guint) i);
}

/* ==========================================================================
 * LrgDateCalendar: the popup
 * ========================================================================== */

/* cal_cell_size: edge of one day cell */
static gfloat
cal_cell_size (LrgWidget *widget)
{
	return roundf (MAX (24.0f, metric (widget, LRG_THEME_METRIC_ROW_HEIGHT)));
}

/* cal_header_h: height of the month title row */
static gfloat
cal_header_h (LrgWidget *widget)
{
	return cal_cell_size (widget) + 4.0f;
}

/* cal_week_h: height of the weekday header */
static gfloat
cal_week_h (LrgWidget *widget)
{
	return roundf (metric (widget, LRG_THEME_METRIC_FONT_SIZE_SMALL) * 1.9f);
}

/* cal_grid_origin: top-left of the grid in canvas coordinates */
static void
cal_grid_origin (LrgWidget *widget,
                 gfloat    *x,
                 gfloat    *y)
{
	*x = lrg_widget_get_world_x (widget) + CAL_PAD;
	*y = lrg_widget_get_world_y (widget) + CAL_PAD + cal_header_h (widget) + cal_week_h (widget);
}

/* cal_button_rect: a header button's rectangle in canvas coordinates */
static void
cal_button_rect (LrgWidget    *widget,
                 gint          button,
                 GrlRectangle *r)
{
	gfloat c = cal_cell_size (widget);

	r->width = c;
	r->height = c;
	r->y = lrg_widget_get_world_y (widget) + CAL_PAD;
	r->x = button == BUTTON_PREV ? lrg_widget_get_world_x (widget) + CAL_PAD
	                             : lrg_widget_get_world_x (widget) + lrg_widget_get_width (widget) - CAL_PAD - c;
}

static gboolean
rect_contains (const GrlRectangle *r,
               gfloat              x,
               gfloat              y)
{
	return x >= r->x && x < r->x + r->width && y >= r->y && y < r->y + r->height;
}

/* cal_button_at: the header button under the point */
static gint
cal_button_at (LrgDateCalendar *self,
               gfloat           x,
               gfloat           y)
{
	GrlRectangle r;

	cal_button_rect (LRG_WIDGET (self), BUTTON_PREV, &r);
	if (rect_contains (&r, x, y))
		return BUTTON_PREV;
	cal_button_rect (LRG_WIDGET (self), BUTTON_NEXT, &r);
	if (rect_contains (&r, x, y))
		return BUTTON_NEXT;
	return BUTTON_NONE;
}

/* cal_cell_at: the grid cell under the point, -1 outside */
static gint
cal_cell_at (LrgDateCalendar *self,
             gfloat           x,
             gfloat           y)
{
	gfloat gx;
	gfloat gy;
	gfloat c = cal_cell_size (LRG_WIDGET (self));
	gint   col;
	gint   row;

	cal_grid_origin (LRG_WIDGET (self), &gx, &gy);
	if (x < gx || y < gy)
		return -1;
	col = (gint) floorf ((x - gx) / c);
	row = (gint) floorf ((y - gy) / c);
	if (col >= CAL_COLS || row >= CAL_ROWS)
		return -1;
	return row * CAL_COLS + col;
}

/* cal_highlight_cell: where the bubble goes: hover, else the keyboard cursor */
static gint
cal_highlight_cell (LrgDateCalendar *self)
{
	if (self->picker == NULL)
		return -1;
	if (self->hover_cell >= 0)
	{
		GDate d;

		cell_day (self->picker, self->hover_cell, &d);
		return day_enabled (self->picker, &d) ? self->hover_cell : -1;
	}
	if (self->keyboard)
		return cell_of (self->picker, &self->cursor);
	return -1;
}

/*
 * cal_turn:
 *
 * Shows another month: the grid slides in from the side of travel
 * (@direction > 0 from the right) and fades up.
 */
static void
cal_turn (LrgDateCalendar *self,
          gint             year,
          gint             month,
          gint             direction)
{
	LrgDatePicker *picker = self->picker;

	if (picker == NULL || (year == picker->view_year && month == picker->view_month))
		return;
	picker->view_year = year;
	picker->view_month = month;
	if (self->stepped && lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE && direction != 0)
	{
		self->slide.value = direction > 0 ? 1.0f : -1.0f;
		self->slide.velocity = 0.0f;
		/* The bubble restarts from its new cell */
		self->hl_alpha.value = 0.0f;
	}
}

/* cal_show_cursor: turn to the cursor's month if it left the view */
static void
cal_show_cursor (LrgDateCalendar *self)
{
	LrgDatePicker *picker = self->picker;
	gint           y = g_date_get_year (&self->cursor);
	gint           m = g_date_get_month (&self->cursor);
	gint           dir;

	if (picker == NULL)
		return;
	dir = (y * 12 + m) - (picker->view_year * 12 + picker->view_month);
	cal_turn (self, y, m, dir);
}

/* cal_move_cursor: keyboard cursor by @days, clamped to min/max */
static void
cal_move_cursor (LrgDateCalendar *self,
                 gint             days)
{
	if (self->picker == NULL)
		return;
	if (days > 0)
		g_date_add_days (&self->cursor, (guint) days);
	else if (days < 0)
		g_date_subtract_days (&self->cursor, (guint) -days);
	clamp_day (self->picker, &self->cursor);
	self->keyboard = TRUE;
	self->hover_cell = -1;
	cal_show_cursor (self);
}

/* cal_move_months: keyboard cursor by whole months (same day, clamped) */
static void
cal_move_months (LrgDateCalendar *self,
                 gint             months)
{
	if (self->picker == NULL)
		return;
	if (months > 0)
		g_date_add_months (&self->cursor, (guint) months);
	else if (months < 0)
		g_date_subtract_months (&self->cursor, (guint) -months);
	clamp_day (self->picker, &self->cursor);
	self->keyboard = TRUE;
	self->hover_cell = -1;
	cal_show_cursor (self);
}

/* cal_page: turn the view by @months without the keyboard (buttons, wheel) */
static void
cal_page (LrgDateCalendar *self,
          gint             months)
{
	LrgDatePicker *picker = self->picker;
	GDate          first;
	GDate          last;
	guint8         dim;

	if (picker == NULL)
		return;
	ensure_view (picker);
	g_date_clear (&first, 1);
	g_date_set_dmy (&first, 1, (GDateMonth) picker->view_month, (GDateYear) picker->view_year);
	if (months > 0)
		g_date_add_months (&first, (guint) months);
	else
		g_date_subtract_months (&first, (guint) -months);

	/* Do not page into months that are entirely out of range */
	dim = g_date_get_days_in_month (g_date_get_month (&first), g_date_get_year (&first));
	last = first;
	g_date_set_day (&last, dim);
	if (!day_enabled (picker, &first) && !day_enabled (picker, &last))
	{
		GDate probe = first;

		clamp_day (picker, &probe);
		if (g_date_get_month (&probe) != g_date_get_month (&first) ||
		    g_date_get_year (&probe) != g_date_get_year (&first))
			return;
	}

	/* Cursor: same day number in the new month, clamped */
	g_date_set_day (&self->cursor, 1);
	g_date_set_month (&self->cursor, g_date_get_month (&first));
	g_date_set_year (&self->cursor, g_date_get_year (&first));
	clamp_day (picker, &self->cursor);
	cal_turn (self, g_date_get_year (&first), g_date_get_month (&first), months);
}

static void
lrg_date_calendar_dispose (GObject *object)
{
	LrgDateCalendar *self = LRG_DATE_CALENDAR (object);

	g_clear_weak_pointer (&self->picker);

	G_OBJECT_CLASS (lrg_date_calendar_parent_class)->dispose (object);
}

static void
lrg_date_calendar_style_defaults (LrgWidget      *widget,
                                  LrgUIPart       part,
                                  LrgWidgetState  state,
                                  LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_POPUP:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS));
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y));
		break;

	case LRG_UI_PART_HEADER:
		/* Month title */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		break;

	case LRG_UI_PART_LABEL:
		/* Weekday names */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		break;

	case LRG_UI_PART_ITEM:
		{
			const GrlColor *hover = lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
			GrlColor        rest = transparent (hover);

			/* Day cells and header buttons; SELECTED is the picked day */
			if (state & LRG_WIDGET_STATE_SELECTED)
			{
				lrg_part_look_set_background (look,
				                              lrg_theme_get_color (theme,
				                                                   (state & LRG_WIDGET_STATE_HOVERED)
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
			lrg_part_look_set_radius (look, G_MAXFLOAT);
			if (state & LRG_WIDGET_STATE_DISABLED)
				lrg_part_look_set_opacity (look, 0.35f);
		}
		break;

	case LRG_UI_PART_INDICATOR:
		/* Today's ring */
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT),
		                          MAX (1.5f, lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH) * 1.5f));
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		break;

	case LRG_UI_PART_ARROW:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, (state & LRG_WIDGET_STATE_HOVERED)
		                                                                ? LRG_THEME_COLOR_TEXT
		                                                                : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_size (look, roundf (lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE) * 0.3f));
		break;

	case LRG_UI_PART_FOCUS:
		{
			GrlColor none = transparent (lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS));

			/* The cursor bubble shows keyboard focus */
			lrg_part_look_set_border (look, &none, 0.0f);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_date_calendar_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_date_calendar_measure (LrgWidget *widget,
                           gfloat    *width,
                           gfloat    *height)
{
	gfloat c = cal_cell_size (widget);

	*width = c * CAL_COLS + CAL_PAD * 2.0f;
	*height = CAL_PAD * 2.0f + cal_header_h (widget) + cal_week_h (widget) + c * CAL_ROWS;
}

static gboolean
lrg_date_calendar_step (LrgWidget *widget,
                        gfloat     delta_time)
{
	LrgDateCalendar *self = LRG_DATE_CALENDAR (widget);
	LrgMotion        motion = lrg_widget_get_motion (widget);
	gint             cell = cal_highlight_cell (self);
	gboolean         busy = FALSE;
	gint             b;

	lrg_spring_set_motion (&self->hl_x, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
	lrg_spring_set_motion (&self->hl_y, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SNAPPY);
	lrg_spring_set_motion (&self->hl_alpha, motion);
	lrg_spring_set_motion (&self->slide, motion == LRG_MOTION_NONE ? LRG_MOTION_NONE : LRG_MOTION_SMOOTH);

	lrg_spring_set_target (&self->hl_alpha, cell >= 0 ? 1.0f : 0.0f);
	lrg_spring_set_target (&self->slide, 0.0f);
	if (cell >= 0)
	{
		lrg_spring_set_target (&self->hl_x, (gfloat) (cell % CAL_COLS));
		lrg_spring_set_target (&self->hl_y, (gfloat) (cell / CAL_COLS));
		/* A bubble that was hidden appears in place, not flying in */
		if (self->hl_alpha.value < 0.05f)
		{
			lrg_spring_snap (&self->hl_x, self->hl_x.target);
			lrg_spring_snap (&self->hl_y, self->hl_y.target);
		}
	}

	for (b = 0; b < 2; b++)
	{
		lrg_spring_set_motion (&self->button_hover[b], motion);
		lrg_spring_set_target (&self->button_hover[b], self->hover_button == b + 1 ? 1.0f : 0.0f);
	}

	if (!self->stepped || motion == LRG_MOTION_NONE)
	{
		lrg_spring_snap (&self->hl_x, self->hl_x.target);
		lrg_spring_snap (&self->hl_y, self->hl_y.target);
		lrg_spring_snap (&self->hl_alpha, self->hl_alpha.target);
		lrg_spring_snap (&self->slide, 0.0f);
		for (b = 0; b < 2; b++)
			lrg_spring_snap (&self->button_hover[b], self->button_hover[b].target);
		self->stepped = TRUE;
		return FALSE;
	}

	busy |= lrg_spring_step (&self->hl_x, delta_time);
	busy |= lrg_spring_step (&self->hl_y, delta_time);
	busy |= lrg_spring_step (&self->hl_alpha, delta_time);
	busy |= lrg_spring_step (&self->slide, delta_time);
	for (b = 0; b < 2; b++)
		busy |= lrg_spring_step (&self->button_hover[b], delta_time);
	return busy;
}

/* paint_chevron: "<" (@dir < 0) or ">" centred on (cx, cy) */
static void
paint_chevron (LrgPaintList      *list,
               const LrgPartLook *look,
               gfloat             cx,
               gfloat             cy,
               gint               dir)
{
	gfloat s = look->size > 0.0f ? look->size : 4.0f;
	gfloat t = MAX (1.5f, s * 0.38f);
	gfloat d = (gfloat) dir;

	lrg_paint_list_line (list, cx - s * 0.45f * d, cy - s, cx + s * 0.45f * d, cy, t, &look->foreground);
	lrg_paint_list_line (list, cx + s * 0.45f * d, cy, cx - s * 0.45f * d, cy + s, t, &look->foreground);
}

/* paint_header: month title and the two turn buttons */
static void
paint_header (LrgDateCalendar *self,
              LrgPaintList    *list)
{
	LrgWidget     *widget = LRG_WIDGET (self);
	LrgDatePicker *picker = self->picker;
	LrgPartLook    title;
	gint           b;

	for (b = BUTTON_PREV; b <= BUTTON_NEXT; b++)
	{
		GrlRectangle   r;
		LrgPartLook    rest;
		LrgPartLook    lit;
		LrgPartLook    look;
		LrgPartLook    arrow;
		LrgWidgetState st = self->hover_button == b ? LRG_WIDGET_STATE_HOVERED : LRG_WIDGET_STATE_NONE;
		gfloat         t = self->stepped ? self->button_hover[b - 1].value : (self->hover_button == b ? 1.0f : 0.0f);

		cal_button_rect (widget, b, &r);
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, LRG_WIDGET_STATE_NONE, &rest);
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, LRG_WIDGET_STATE_HOVERED, &lit);
		lrg_part_look_lerp (&rest, &lit, CLAMP (t, 0.0f, 1.0f), &look);
		if (self->press_button == b && self->hover_button == b)
			lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM,
			                                   LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED, &look);
		lrg_widget_paint_box (widget, list, &look, r.x + 2.0f, r.y + 2.0f, r.width - 4.0f, r.height - 4.0f);

		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ARROW, st, &arrow);
		paint_chevron (list, &arrow, r.x + r.width * 0.5f, r.y + r.height * 0.5f, b == BUTTON_PREV ? -1 : 1);
	}

	if (picker != NULL)
	{
		g_autoptr(GDateTime) first = g_date_time_new_local (picker->view_year, picker->view_month, 1, 0, 0, 0.0);
		g_autofree gchar    *text = first != NULL ? g_date_time_format (first, "%B %Y") : NULL;
		GrlRectangle         r;

		cal_button_rect (widget, BUTTON_PREV, &r);
		lrg_widget_resolve_look (widget, LRG_UI_PART_HEADER, &title);
		paint_text_centered (list, widget, text, lrg_widget_get_world_x (widget) + lrg_widget_get_width (widget) * 0.5f,
		                     r.y + r.height * 0.5f, metric (widget, LRG_THEME_METRIC_FONT_SIZE), &title.foreground);
	}
}

/* paint_weekdays: "Mon".."Sun" abbreviated to two characters */
static void
paint_weekdays (LrgDateCalendar *self,
                LrgPaintList    *list)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	LrgPartLook  label;
	gfloat       gx;
	gfloat       gy;
	gfloat       c = cal_cell_size (widget);
	gfloat       wh = cal_week_h (widget);
	gint         i;

	cal_grid_origin (widget, &gx, &gy);
	lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);

	/* 2024-01-01 was a Monday */
	for (i = 0; i < CAL_COLS; i++)
	{
		g_autoptr(GDateTime) day = g_date_time_new_utc (2024, 1, 1 + i, 0, 0, 0.0);
		g_autofree gchar    *name = g_date_time_format (day, "%a");
		g_autofree gchar    *shortname = name != NULL ? g_utf8_substring (name, 0, MIN (2, g_utf8_strlen (name, -1)))
		                                              : NULL;

		paint_text_centered (list, widget, shortname, gx + c * ((gfloat) i + 0.5f), gy - wh * 0.5f,
		                     metric (widget, LRG_THEME_METRIC_FONT_SIZE_SMALL), &label.foreground);
	}
}

/* paint_grid: highlight bubble, then every day cell */
static void
paint_grid (LrgDateCalendar *self,
            LrgPaintList    *list)
{
	LrgWidget     *widget = LRG_WIDGET (self);
	LrgDatePicker *picker = self->picker;
	GDate          selected;
	gboolean       has_selected = FALSE;
	gfloat         gx;
	gfloat         gy;
	gfloat         c = cal_cell_size (widget);
	gfloat         inset = 2.0f;
	gfloat         size = metric (widget, LRG_THEME_METRIC_FONT_SIZE);
	gfloat         slide = self->stepped ? self->slide.value : 0.0f;
	guint          mark;
	gint           i;

	if (picker == NULL)
		return;
	cal_grid_origin (widget, &gx, &gy);
	if (picker->date != NULL)
	{
		gdate_from_datetime (picker->date, &selected);
		has_selected = TRUE;
	}

	lrg_paint_list_push_clip (list, gx - 2.0f, gy - 2.0f, c * CAL_COLS + 4.0f, c * CAL_ROWS + 4.0f);
	mark = lrg_paint_list_begin_group (list);

	/* Bubble under the hovered / cursor day */
	{
		gfloat alpha = self->stepped ? self->hl_alpha.value : (cal_highlight_cell (self) >= 0 ? 1.0f : 0.0f);

		if (alpha > 0.01f)
		{
			LrgPartLook bubble;
			gfloat      bx = self->stepped ? self->hl_x.value : (gfloat) (cal_highlight_cell (self) % CAL_COLS);
			gfloat      by = self->stepped ? self->hl_y.value : (gfloat) (cal_highlight_cell (self) / CAL_COLS);

			lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, LRG_WIDGET_STATE_HOVERED, &bubble);
			lrg_paint_list_push_opacity (list, CLAMP (alpha, 0.0f, 1.0f));
			lrg_paint_list_rect (list, gx + bx * c + inset, gy + by * c + inset, c - inset * 2.0f,
			                     c - inset * 2.0f, G_MAXFLOAT, &bubble.background);
			lrg_paint_list_pop_opacity (list);
		}
	}

	for (i = 0; i < CAL_CELLS; i++)
	{
		GDate          day;
		gchar          text[4];
		LrgWidgetState state = LRG_WIDGET_STATE_NONE;
		LrgPartLook    look;
		gfloat         cx = gx + c * ((gfloat) (i % CAL_COLS) + 0.5f);
		gfloat         cy = gy + c * ((gfloat) (i / CAL_COLS) + 0.5f);
		gboolean       in_month;
		gboolean       enabled;
		const GrlColor *fg;

		cell_day (picker, i, &day);
		in_month = g_date_get_month (&day) == (GDateMonth) picker->view_month;
		enabled = day_enabled (picker, &day);
		if (!enabled)
			state |= LRG_WIDGET_STATE_DISABLED;
		if (has_selected && g_date_compare (&day, &selected) == 0)
			state |= LRG_WIDGET_STATE_SELECTED;
		if (i == self->press_cell && i == self->hover_cell)
			state |= LRG_WIDGET_STATE_PRESSED | LRG_WIDGET_STATE_HOVERED;
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, state, &look);

		if (look.opacity < 1.0f)
			lrg_paint_list_push_opacity (list, look.opacity);

		/* Selected disc, or the pressed flash */
		if (state & (LRG_WIDGET_STATE_SELECTED | LRG_WIDGET_STATE_PRESSED))
			lrg_paint_list_rect (list, cx - c * 0.5f + inset, cy - c * 0.5f + inset, c - inset * 2.0f,
			                     c - inset * 2.0f, G_MAXFLOAT, &look.background);

		/* Today's ring */
		if (g_date_compare (&day, &self->today) == 0)
		{
			LrgPartLook ring;

			lrg_widget_resolve_look (widget, LRG_UI_PART_INDICATOR, &ring);
			lrg_paint_list_rect_ex (list, cx - c * 0.5f + inset, cy - c * 0.5f + inset, c - inset * 2.0f,
			                        c - inset * 2.0f, &ring.radii, NULL, NULL, ring.border_width, &ring.border);
		}

		fg = &look.foreground;
		if (!in_month && !(state & LRG_WIDGET_STATE_SELECTED))
			fg = color (widget, LRG_THEME_COLOR_TEXT_MUTED);
		g_snprintf (text, sizeof (text), "%d", g_date_get_day (&day));
		paint_text_centered (list, widget, text, cx, cy, size, fg);

		if (look.opacity < 1.0f)
			lrg_paint_list_pop_opacity (list);
	}

	/* Month turn: the grid slides in from the side of travel */
	if (slide != 0.0f)
		lrg_paint_list_transform_group (list, mark, 1.0f, 0.0f, 0.0f, slide * c * 2.0f, 0.0f,
		                                CLAMP (1.0f - fabsf (slide), 0.0f, 1.0f));
	lrg_paint_list_pop_clip (list);
}

static void
lrg_date_calendar_paint (LrgWidget    *widget,
                         LrgPaintList *list)
{
	LrgDateCalendar *self = LRG_DATE_CALENDAR (widget);
	LrgPartLook      surface;

	lrg_widget_resolve_look (widget, LRG_UI_PART_POPUP, &surface);
	lrg_widget_paint_box (widget, list, &surface, lrg_widget_get_world_x (widget), lrg_widget_get_world_y (widget),
	                      lrg_widget_get_width (widget), lrg_widget_get_height (widget));

	paint_header (self, list);
	paint_weekdays (self, list);
	paint_grid (self, list);
}

/* cal_close: Escape / B: close, focus ring back on the field */
static void
cal_close (LrgDateCalendar *self)
{
	LrgDatePicker *picker = self->picker;
	LrgCanvas     *canvas;

	if (picker == NULL)
		return;
	canvas = find_canvas (LRG_WIDGET (self));
	lrg_date_picker_popdown (picker);
	if (canvas != NULL && lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (picker))
		lrg_widget_set_state_flags (LRG_WIDGET (picker), LRG_WIDGET_STATE_FOCUS_VISIBLE);
}

static gboolean
lrg_date_calendar_handle_event (LrgWidget        *widget,
                                const LrgUIEvent *event)
{
	LrgDateCalendar *self = LRG_DATE_CALENDAR (widget);
	gfloat           ex = lrg_ui_event_get_x (event);
	gfloat           ey = lrg_ui_event_get_y (event);
	gboolean         shift = (lrg_ui_event_get_modifiers (event) & LRG_INPUT_MODIFIER_SHIFT) != 0;

	if (self->picker == NULL)
		return FALSE;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		if (lrg_widget_contains_point (widget, ex, ey))
		{
			self->hover_cell = cal_cell_at (self, ex, ey);
			self->hover_button = cal_button_at (self, ex, ey);
		}
		else
		{
			self->hover_cell = -1;
			self->hover_button = BUTTON_NONE;
		}
		return TRUE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_cell = -1;
		self->hover_button = BUTTON_NONE;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return TRUE;
		self->hover_cell = cal_cell_at (self, ex, ey);
		self->hover_button = cal_button_at (self, ex, ey);
		self->press_cell = self->hover_cell;
		self->press_button = self->hover_button;
		self->keyboard = FALSE;
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		{
			gint cell = cal_cell_at (self, ex, ey);
			gint button = cal_button_at (self, ex, ey);
			gint pressed_cell = self->press_cell;
			gint pressed_button = self->press_button;

			if (lrg_ui_event_get_button (event) != 0)
				return TRUE;
			self->press_cell = -1;
			self->press_button = BUTTON_NONE;

			if (pressed_button != BUTTON_NONE && pressed_button == button)
				cal_page (self, button == BUTTON_PREV ? -1 : 1);
			else if (pressed_cell >= 0 && pressed_cell == cell)
			{
				GDate day;

				cell_day (self->picker, cell, &day);
				if (day_enabled (self->picker, &day))
					picker_select_day (self->picker, &day, FALSE);
			}
			return TRUE;
		}

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat dy = lrg_ui_event_get_scroll_y (event);

			/* Wheel up shows the previous month */
			if (dy != 0.0f)
				cal_page (self, dy > 0.0f ? -1 : 1);
			return TRUE;
		}

	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_LEFT:
			cal_move_cursor (self, -1);
			return TRUE;
		case GRL_KEY_RIGHT:
			cal_move_cursor (self, 1);
			return TRUE;
		case GRL_KEY_UP:
			cal_move_cursor (self, -7);
			return TRUE;
		case GRL_KEY_DOWN:
			cal_move_cursor (self, 7);
			return TRUE;
		case GRL_KEY_PAGE_UP:
			cal_move_months (self, shift ? -12 : -1);
			return TRUE;
		case GRL_KEY_PAGE_DOWN:
			cal_move_months (self, shift ? 12 : 1);
			return TRUE;
		case GRL_KEY_HOME:
			cal_move_cursor (self, 1 - (gint) g_date_get_day (&self->cursor));
			return TRUE;
		case GRL_KEY_END:
			cal_move_cursor (self, (gint) g_date_get_days_in_month (g_date_get_month (&self->cursor),
			                                                        g_date_get_year (&self->cursor)) -
			                       (gint) g_date_get_day (&self->cursor));
			return TRUE;
		case GRL_KEY_ENTER:
		case GRL_KEY_KP_ENTER:
		case GRL_KEY_SPACE:
			if (day_enabled (self->picker, &self->cursor))
				picker_select_day (self->picker, &self->cursor, TRUE);
			return TRUE;
		default:
			return FALSE;
		}

	case LRG_UI_EVENT_ACTIVATE:
		if (day_enabled (self->picker, &self->cursor))
			picker_select_day (self->picker, &self->cursor, TRUE);
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		cal_close (self);
		return TRUE;

	default:
		return FALSE;
	}
}

static void
lrg_date_calendar_class_init (LrgDateCalendarClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_date_calendar_dispose;
	widget_class->style_defaults = lrg_date_calendar_style_defaults;
	widget_class->measure = lrg_date_calendar_measure;
	widget_class->paint = lrg_date_calendar_paint;
	widget_class->step = lrg_date_calendar_step;
	widget_class->handle_event = lrg_date_calendar_handle_event;
}

static void
lrg_date_calendar_init (LrgDateCalendar *self)
{
	gint b;

	g_date_clear (&self->cursor, 1);
	g_date_clear (&self->today, 1);
	self->hover_cell = -1;
	self->press_cell = -1;
	lrg_spring_init (&self->hl_x, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->hl_y, 0.0f, LRG_MOTION_SNAPPY);
	lrg_spring_init (&self->hl_alpha, 0.0f, LRG_MOTION_SMOOTH);
	lrg_spring_init (&self->slide, 0.0f, LRG_MOTION_SMOOTH);
	for (b = 0; b < 2; b++)
		lrg_spring_init (&self->button_hover[b], 0.0f, LRG_MOTION_SMOOTH);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Popup open / close
 * ========================================================================== */

static void
forget_popup (LrgDatePicker *self)
{
	if (self->canvas != NULL && self->dismissed_id != 0)
		g_signal_handler_disconnect (self->canvas, self->dismissed_id);
	self->dismissed_id = 0;
	g_clear_weak_pointer (&self->canvas);

	if (self->popup != NULL)
	{
		g_clear_object (&self->popup);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POPUP_OPEN]);
	}
}

static void
on_popup_dismissed (LrgCanvas *canvas,
                    LrgWidget *popup,
                    gpointer   user_data)
{
	LrgDatePicker *self = LRG_DATE_PICKER (user_data);

	if (self->popup != NULL && popup == LRG_WIDGET (self->popup))
		forget_popup (self);
}

/* on_state_changed: a press on the field that closed the calendar must
 * not reopen it (see LrgComboBox) */
static void
on_state_changed (LrgWidget      *widget,
                  LrgWidgetState  previous,
                  gpointer        user_data)
{
	LrgDatePicker  *self = LRG_DATE_PICKER (widget);
	LrgWidgetState  now = lrg_widget_get_state_flags (widget);

	if ((previous & LRG_WIDGET_STATE_OPEN) && !(now & LRG_WIDGET_STATE_OPEN) &&
	    (now & LRG_WIDGET_STATE_HOVERED))
		self->swallow_press = TRUE;
}

/*
 * open_popup:
 *
 * Builds the calendar on the month of the date (or today), with the
 * keyboard cursor on the date, and gives it focus (ring when @keyboard).
 */
static void
open_popup (LrgDatePicker *self,
            gboolean       keyboard)
{
	LrgCanvas       *canvas;
	LrgDateCalendar *cal;
	const GQuark    *classes;
	guint            n_classes;
	guint            c;

	if (self->popup != NULL)
		return;
	canvas = find_canvas (LRG_WIDGET (self));
	if (canvas == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN_UI, "date picker is not on a canvas; cannot open its calendar");
		return;
	}

	cal = g_object_new (LRG_TYPE_DATE_CALENDAR, NULL);
	g_set_weak_pointer (&cal->picker, self);
	lrg_widget_set_theme (LRG_WIDGET (cal), lrg_widget_get_theme (LRG_WIDGET (self)));
	lrg_widget_set_appear (LRG_WIDGET (cal), LRG_APPEAR_FLY_DOWN, 0.0f);
	classes = lrg_widget_get_classes (LRG_WIDGET (self), &n_classes);
	for (c = 0; c < n_classes; c++)
		lrg_widget_add_class (LRG_WIDGET (cal), g_quark_to_string (classes[c]));

	/* Today, the cursor and the month on show */
	today_gdate (self, &cal->today);
	if (self->date != NULL)
		gdate_from_datetime (self->date, &cal->cursor);
	else
	{
		cal->cursor = cal->today;
		clamp_day (self, &cal->cursor);
	}
	self->view_year = g_date_get_year (&cal->cursor);
	self->view_month = g_date_get_month (&cal->cursor);
	cal->keyboard = keyboard;

	self->popup = cal;
	g_set_weak_pointer (&self->canvas, canvas);
	self->dismissed_id = g_signal_connect_object (canvas, "popup-dismissed",
	                                              G_CALLBACK (on_popup_dismissed), self, 0);
	lrg_canvas_show_popup (canvas, LRG_WIDGET (cal), LRG_WIDGET (self), LRG_POPUP_PLACEMENT_BELOW, FALSE);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POPUP_OPEN]);

	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (cal));
	if (keyboard)
		lrg_widget_set_state_flags (LRG_WIDGET (cal), LRG_WIDGET_STATE_FOCUS_VISIBLE);
}

/* picker_select_day: a day was picked: close, then set the date */
static void
picker_select_day (LrgDatePicker *self,
                   const GDate   *day,
                   gboolean       keyboard)
{
	g_autoptr(GDateTime) dt = datetime_from_gdate (day);

	g_object_ref (self);
	lrg_date_picker_popdown (self);
	if (keyboard)
	{
		LrgCanvas *canvas = find_canvas (LRG_WIDGET (self));

		if (canvas != NULL && lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (self))
			lrg_widget_set_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_FOCUS_VISIBLE);
	}
	lrg_date_picker_set_date (self, dt);
	g_object_unref (self);
}

/* ==========================================================================
 * LrgDatePicker vfuncs
 * ========================================================================== */

static void
lrg_date_picker_style_defaults (LrgWidget      *widget,
                                LrgUIPart       part,
                                LrgWidgetState  state,
                                LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_BOX:
		{
			LrgThemeColor border = LRG_THEME_COLOR_BORDER;

			if (state & (LRG_WIDGET_STATE_OPEN | LRG_WIDGET_STATE_FOCUSED))
				border = LRG_THEME_COLOR_ACCENT;
			else if (state & LRG_WIDGET_STATE_HOVERED)
				border = LRG_THEME_COLOR_TEXT_MUTED;
			lrg_part_look_set_background (look, lrg_theme_get_color (theme,
			                                                         (state & LRG_WIDGET_STATE_PRESSED)
			                                                         ? LRG_THEME_COLOR_CONTROL_PRESSED
			                                                         : LRG_THEME_COLOR_INPUT));
			lrg_part_look_set_border (look, lrg_theme_get_color (theme, border),
			                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
			lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
			if (state & LRG_WIDGET_STATE_DISABLED)
				lrg_part_look_set_opacity (look, 0.45f);
		}
		break;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		break;

	case LRG_UI_PART_ARROW:
		/* The calendar icon */
		lrg_part_look_set_foreground (look,
		                              lrg_theme_get_color (theme,
		                                                   (state & LRG_WIDGET_STATE_OPEN)
		                                                   ? LRG_THEME_COLOR_ACCENT
		                                                   : (state & LRG_WIDGET_STATE_HOVERED)
		                                                     ? LRG_THEME_COLOR_TEXT
		                                                     : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_size (look, roundf (lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE)));
		break;

	default:
		LRG_WIDGET_CLASS (lrg_date_picker_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

/* sample_text: the formatted width-defining sample (a long date) */
static gchar *
sample_text (LrgDatePicker *self)
{
	g_autoptr(GDateTime) sample = g_date_time_new_local (2026, 9, 28, 0, 0, 0.0);

	return sample != NULL ? g_date_time_format (sample, self->format) : NULL;
}

static void
lrg_date_picker_measure (LrgWidget *widget,
                         gfloat    *width,
                         gfloat    *height)
{
	LrgDatePicker    *self = LRG_DATE_PICKER (widget);
	gfloat            size = metric (widget, LRG_THEME_METRIC_FONT_SIZE);
	gfloat            pad = metric (widget, LRG_THEME_METRIC_PADDING_X);
	g_autofree gchar *sample = sample_text (self);
	gfloat            tw;

	tw = MAX (text_width (widget, sample, size),
	          text_width (widget, self->placeholder != NULL ? self->placeholder : DEFAULT_PLACEHOLDER, size));
	*width = roundf (pad * 2.0f + tw + metric (widget, LRG_THEME_METRIC_SPACING) + size);
	*height = metric (widget, LRG_THEME_METRIC_CONTROL_HEIGHT);
}

/* paint_calendar_icon: a tiny calendar page centred on (cx, cy) */
static void
paint_calendar_icon (LrgPaintList      *list,
                     const LrgPartLook *look,
                     gfloat             cx,
                     gfloat             cy)
{
	gfloat         s = look->size > 0.0f ? look->size : 14.0f;
	gfloat         w = roundf (s * 0.95f);
	gfloat         h = roundf (s * 0.85f);
	gfloat         x = roundf (cx - w * 0.5f);
	gfloat         y = roundf (cy - h * 0.5f + s * 0.06f);
	gfloat         t = MAX (1.25f, s * 0.09f);
	LrgCornerRadii radii;
	LrgCornerRadii top;

	lrg_corner_radii_init_uniform (&radii, s * 0.16f);
	top = radii;
	top.bottom_left = 0.0f;
	top.bottom_right = 0.0f;

	/* Page outline, header band, two binder rings, two "days" */
	lrg_paint_list_rect_ex (list, x, y, w, h, &radii, NULL, NULL, t, &look->foreground);
	lrg_paint_list_rect_ex (list, x, y, w, roundf (h * 0.32f), &top, &look->foreground, NULL, 0.0f, NULL);
	lrg_paint_list_line (list, x + w * 0.3f, y - s * 0.12f, x + w * 0.3f, y + s * 0.08f, t, &look->foreground);
	lrg_paint_list_line (list, x + w * 0.7f, y - s * 0.12f, x + w * 0.7f, y + s * 0.08f, t, &look->foreground);
	lrg_paint_list_rect (list, x + w * 0.22f, y + h * 0.52f, w * 0.18f, h * 0.18f, 1.0f, &look->foreground);
	lrg_paint_list_rect (list, x + w * 0.58f, y + h * 0.52f, w * 0.18f, h * 0.18f, 1.0f, &look->foreground);
}

static void
lrg_date_picker_paint (LrgWidget    *widget,
                       LrgPaintList *list)
{
	LrgDatePicker    *self = LRG_DATE_PICKER (widget);
	LrgTheme         *theme = lrg_widget_get_theme (widget);
	GrlFont          *font = lrg_theme_get_ui_font (theme);
	LrgPartLook       box;
	LrgPartLook       label;
	LrgPartLook       icon;
	gfloat            x = lrg_widget_get_world_x (widget);
	gfloat            y = lrg_widget_get_world_y (widget);
	gfloat            w = lrg_widget_get_width (widget);
	gfloat            h = lrg_widget_get_height (widget);
	gfloat            size = metric (widget, LRG_THEME_METRIC_FONT_SIZE);
	gfloat            pad = metric (widget, LRG_THEME_METRIC_PADDING_X);
	gfloat            th = size;
	g_autofree gchar *text = lrg_date_picker_get_text (self);
	const gchar      *shown = text != NULL ? text : (self->placeholder != NULL ? self->placeholder
	                                                                           : DEFAULT_PLACEHOLDER);

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, x, y, w, h);
	if (box.opacity < 1.0f)
		lrg_paint_list_push_opacity (list, box.opacity);

	/* Date or placeholder, clipped before the icon */
	lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
	lrg_paint_measure_text (font, shown, size, 1.0f, NULL, &th);
	lrg_paint_list_push_clip (list, x + pad, y, MAX (0.0f, w - pad * 2.0f - size), h);
	lrg_paint_list_text (list, font, shown, roundf (x + pad), roundf (y + (h - th) * 0.5f), size, 1.0f,
	                     text != NULL ? &label.foreground : color (widget, LRG_THEME_COLOR_TEXT_MUTED));
	lrg_paint_list_pop_clip (list);

	lrg_widget_resolve_look (widget, LRG_UI_PART_ARROW, &icon);
	paint_calendar_icon (list, &icon, x + w - pad - size * 0.5f, y + h * 0.5f);

	if (box.opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

static gboolean
lrg_date_picker_handle_event (LrgWidget        *widget,
                              const LrgUIEvent *event)
{
	LrgDatePicker *self = LRG_DATE_PICKER (widget);
	gboolean       inside = lrg_widget_contains_point (widget, lrg_ui_event_get_x (event),
	                                                   lrg_ui_event_get_y (event));

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_POINTER_ENTER:
		self->swallow_press = FALSE;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0 || !inside)
			return FALSE;
		if (self->swallow_press)
			self->swallow_press = FALSE;
		else if (self->popup != NULL)
			lrg_date_picker_popdown (self);
		else
			open_popup (self, FALSE);
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		return lrg_ui_event_get_button (event) == 0;

	case LRG_UI_EVENT_ACTIVATE:
		if (self->popup != NULL)
			lrg_date_picker_popdown (self);
		else
			open_popup (self, TRUE);
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		/* Alt+Down, like a combo box, opens from the keyboard too */
		if (lrg_ui_event_get_key (event) == GRL_KEY_DOWN &&
		    (lrg_ui_event_get_modifiers (event) & LRG_INPUT_MODIFIER_ALT) && self->popup == NULL)
		{
			open_popup (self, TRUE);
			return TRUE;
		}
		return FALSE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_date_picker_dispose (GObject *object)
{
	LrgDatePicker *self = LRG_DATE_PICKER (object);

	if (self->popup != NULL && self->canvas != NULL)
	{
		g_autoptr(LrgCanvas) canvas = g_object_ref (self->canvas);
		g_autoptr(LrgWidget) popup = LRG_WIDGET (g_object_ref (self->popup));

		forget_popup (self);
		lrg_canvas_dismiss_popup (canvas, popup);
	}
	forget_popup (self);

	G_OBJECT_CLASS (lrg_date_picker_parent_class)->dispose (object);
}

static void
lrg_date_picker_finalize (GObject *object)
{
	LrgDatePicker *self = LRG_DATE_PICKER (object);

	g_clear_pointer (&self->date, g_date_time_unref);
	g_clear_pointer (&self->min_date, g_date_time_unref);
	g_clear_pointer (&self->max_date, g_date_time_unref);
	g_clear_pointer (&self->today, g_date_time_unref);
	g_clear_pointer (&self->format, g_free);
	g_clear_pointer (&self->placeholder, g_free);

	G_OBJECT_CLASS (lrg_date_picker_parent_class)->finalize (object);
}

static void
lrg_date_picker_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgDatePicker *self = LRG_DATE_PICKER (object);

	switch (prop_id)
	{
	case PROP_DATE:
		g_value_set_boxed (value, self->date);
		break;
	case PROP_FORMAT:
		g_value_set_string (value, self->format);
		break;
	case PROP_PLACEHOLDER:
		g_value_set_string (value, self->placeholder);
		break;
	case PROP_MIN_DATE:
		g_value_set_boxed (value, self->min_date);
		break;
	case PROP_MAX_DATE:
		g_value_set_boxed (value, self->max_date);
		break;
	case PROP_TODAY:
		g_value_set_boxed (value, self->today);
		break;
	case PROP_POPUP_OPEN:
		g_value_set_boolean (value, self->popup != NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_date_picker_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgDatePicker *self = LRG_DATE_PICKER (object);

	switch (prop_id)
	{
	case PROP_DATE:
		lrg_date_picker_set_date (self, g_value_get_boxed (value));
		break;
	case PROP_FORMAT:
		lrg_date_picker_set_format (self, g_value_get_string (value));
		break;
	case PROP_PLACEHOLDER:
		lrg_date_picker_set_placeholder (self, g_value_get_string (value));
		break;
	case PROP_MIN_DATE:
		lrg_date_picker_set_min_date (self, g_value_get_boxed (value));
		break;
	case PROP_MAX_DATE:
		lrg_date_picker_set_max_date (self, g_value_get_boxed (value));
		break;
	case PROP_TODAY:
		lrg_date_picker_set_today (self, g_value_get_boxed (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_date_picker_class_init (LrgDatePickerClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_date_picker_dispose;
	object_class->finalize = lrg_date_picker_finalize;
	object_class->get_property = lrg_date_picker_get_property;
	object_class->set_property = lrg_date_picker_set_property;

	widget_class->style_defaults = lrg_date_picker_style_defaults;
	widget_class->measure = lrg_date_picker_measure;
	widget_class->paint = lrg_date_picker_paint;
	widget_class->handle_event = lrg_date_picker_handle_event;

	/**
	 * LrgDatePicker:date:
	 *
	 * The picked day as a #GDateTime at local midnight, or %NULL.
	 */
	properties[PROP_DATE] =
		g_param_spec_boxed ("date", NULL, NULL, G_TYPE_DATE_TIME,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDatePicker:format:
	 *
	 * g_date_time_format() pattern for the field, "%Y-%m-%d" by default.
	 */
	properties[PROP_FORMAT] =
		g_param_spec_string ("format", NULL, NULL, DEFAULT_FORMAT,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDatePicker:placeholder:
	 *
	 * Muted text shown while no date is set; %NULL shows "Select a date".
	 */
	properties[PROP_PLACEHOLDER] =
		g_param_spec_string ("placeholder", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDatePicker:min-date:
	 *
	 * Earliest pickable day, or %NULL.
	 */
	properties[PROP_MIN_DATE] =
		g_param_spec_boxed ("min-date", NULL, NULL, G_TYPE_DATE_TIME,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDatePicker:max-date:
	 *
	 * Latest pickable day, or %NULL.
	 */
	properties[PROP_MAX_DATE] =
		g_param_spec_boxed ("max-date", NULL, NULL, G_TYPE_DATE_TIME,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDatePicker:today:
	 *
	 * The day ringed as today, or %NULL for the system date.
	 */
	properties[PROP_TODAY] =
		g_param_spec_boxed ("today", NULL, NULL, G_TYPE_DATE_TIME,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgDatePicker:popup-open:
	 *
	 * Whether the calendar is showing.
	 */
	properties[PROP_POPUP_OPEN] =
		g_param_spec_boolean ("popup-open", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgDatePicker::date-changed:
	 * @self: the date picker
	 *
	 * Emitted when the picked day changes, by the user, by clamping to
	 * new limits, or from code.
	 */
	signals[SIGNAL_DATE_CHANGED] =
		g_signal_new ("date-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_date_picker_init (LrgDatePicker *self)
{
	self->format = g_strdup (DEFAULT_FORMAT);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
	g_signal_connect (self, "state-changed", G_CALLBACK (on_state_changed), NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgDatePicker *
lrg_date_picker_new (void)
{
	return g_object_new (LRG_TYPE_DATE_PICKER, NULL);
}

GDateTime *
lrg_date_picker_get_date (LrgDatePicker *self)
{
	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), NULL);

	return self->date;
}

void
lrg_date_picker_set_date (LrgDatePicker *self,
                          GDateTime     *date)
{
	g_autoptr(GDateTime) normal = NULL;

	g_return_if_fail (LRG_IS_DATE_PICKER (self));

	/* Date only, clamped into range */
	if (date != NULL)
	{
		GDate d;

		gdate_from_datetime (date, &d);
		clamp_day (self, &d);
		normal = datetime_from_gdate (&d);
	}
	if (same_day (normal, self->date))
		return;

	g_clear_pointer (&self->date, g_date_time_unref);
	self->date = g_steal_pointer (&normal);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DATE]);
	g_signal_emit (self, signals[SIGNAL_DATE_CHANGED], 0);
}

void
lrg_date_picker_set_date_ymd (LrgDatePicker *self,
                              gint           year,
                              gint           month,
                              gint           day)
{
	g_autoptr(GDateTime) dt = NULL;

	g_return_if_fail (LRG_IS_DATE_PICKER (self));

	if (!g_date_valid_dmy ((GDateDay) day, (GDateMonth) month, (GDateYear) year))
		return;
	dt = g_date_time_new_local (year, month, day, 0, 0, 0.0);
	lrg_date_picker_set_date (self, dt);
}

gboolean
lrg_date_picker_get_date_ymd (LrgDatePicker *self,
                              gint          *year,
                              gint          *month,
                              gint          *day)
{
	gint y;
	gint m;
	gint d;

	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), FALSE);

	if (self->date == NULL)
		return FALSE;
	g_date_time_get_ymd (self->date, &y, &m, &d);
	if (year != NULL)
		*year = y;
	if (month != NULL)
		*month = m;
	if (day != NULL)
		*day = d;
	return TRUE;
}

gchar *
lrg_date_picker_get_text (LrgDatePicker *self)
{
	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), NULL);

	if (self->date == NULL)
		return NULL;
	return g_date_time_format (self->date, self->format);
}

const gchar *
lrg_date_picker_get_format (LrgDatePicker *self)
{
	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), NULL);

	return self->format;
}

void
lrg_date_picker_set_format (LrgDatePicker *self,
                            const gchar   *format)
{
	g_return_if_fail (LRG_IS_DATE_PICKER (self));

	if (format == NULL)
		format = DEFAULT_FORMAT;
	if (g_strcmp0 (format, self->format) == 0)
		return;
	g_free (self->format);
	self->format = g_strdup (format);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FORMAT]);
}

const gchar *
lrg_date_picker_get_placeholder (LrgDatePicker *self)
{
	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), NULL);

	return self->placeholder;
}

void
lrg_date_picker_set_placeholder (LrgDatePicker *self,
                                 const gchar   *placeholder)
{
	g_return_if_fail (LRG_IS_DATE_PICKER (self));

	if (g_strcmp0 (placeholder, self->placeholder) == 0)
		return;
	g_free (self->placeholder);
	self->placeholder = g_strdup (placeholder);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLACEHOLDER]);
}

GDateTime *
lrg_date_picker_get_min_date (LrgDatePicker *self)
{
	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), NULL);

	return self->min_date;
}

/* set_limit: store a min / max day and re-clamp the date */
static void
set_limit (LrgDatePicker  *self,
           GDateTime     **slot,
           GDateTime      *date,
           guint           prop)
{
	if (same_day (*slot, date))
		return;
	g_clear_pointer (slot, g_date_time_unref);
	*slot = date_only (date);
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);

	if (self->date != NULL)
	{
		g_autoptr(GDateTime) current = g_date_time_ref (self->date);

		lrg_date_picker_set_date (self, current);
	}
}

void
lrg_date_picker_set_min_date (LrgDatePicker *self,
                              GDateTime     *date)
{
	g_return_if_fail (LRG_IS_DATE_PICKER (self));

	set_limit (self, &self->min_date, date, PROP_MIN_DATE);
}

GDateTime *
lrg_date_picker_get_max_date (LrgDatePicker *self)
{
	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), NULL);

	return self->max_date;
}

void
lrg_date_picker_set_max_date (LrgDatePicker *self,
                              GDateTime     *date)
{
	g_return_if_fail (LRG_IS_DATE_PICKER (self));

	set_limit (self, &self->max_date, date, PROP_MAX_DATE);
}

GDateTime *
lrg_date_picker_get_today (LrgDatePicker *self)
{
	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), NULL);

	return self->today;
}

void
lrg_date_picker_set_today (LrgDatePicker *self,
                           GDateTime     *today)
{
	g_return_if_fail (LRG_IS_DATE_PICKER (self));

	if (same_day (self->today, today))
		return;
	g_clear_pointer (&self->today, g_date_time_unref);
	self->today = date_only (today);
	if (self->popup != NULL)
		today_gdate (self, &self->popup->today);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TODAY]);
}

void
lrg_date_picker_popup (LrgDatePicker *self)
{
	g_return_if_fail (LRG_IS_DATE_PICKER (self));

	open_popup (self, FALSE);
}

void
lrg_date_picker_popdown (LrgDatePicker *self)
{
	g_return_if_fail (LRG_IS_DATE_PICKER (self));

	if (self->popup == NULL)
		return;
	if (self->canvas != NULL)
	{
		g_autoptr(LrgCanvas) canvas = g_object_ref (self->canvas);
		g_autoptr(LrgWidget) popup = LRG_WIDGET (g_object_ref (self->popup));

		lrg_canvas_dismiss_popup (canvas, popup);
	}
	forget_popup (self);
}

gboolean
lrg_date_picker_get_popup_open (LrgDatePicker *self)
{
	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), FALSE);

	return self->popup != NULL;
}

void
lrg_date_picker_get_visible_month (LrgDatePicker *self,
                                   gint          *year,
                                   gint          *month)
{
	g_return_if_fail (LRG_IS_DATE_PICKER (self));

	ensure_view (self);
	if (year != NULL)
		*year = self->view_year;
	if (month != NULL)
		*month = self->view_month;
}

void
lrg_date_picker_show_month (LrgDatePicker *self,
                            gint           year,
                            gint           month)
{
	g_return_if_fail (LRG_IS_DATE_PICKER (self));
	g_return_if_fail (month >= 1 && month <= 12);

	ensure_view (self);
	if (self->popup == NULL)
	{
		self->view_year = year;
		self->view_month = month;
		return;
	}

	/* Cursor on the first pickable day of that month */
	g_date_clear (&self->popup->cursor, 1);
	g_date_set_dmy (&self->popup->cursor, 1, (GDateMonth) month, (GDateYear) year);
	clamp_day (self, &self->popup->cursor);
	cal_turn (self->popup, year, month, (year * 12 + month) - (self->view_year * 12 + self->view_month));
}

GDateTime *
lrg_date_picker_get_cursor_date (LrgDatePicker *self)
{
	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), NULL);

	if (self->popup == NULL)
		return NULL;
	return datetime_from_gdate (&self->popup->cursor);
}

gboolean
lrg_date_picker_get_day_bounds (LrgDatePicker *self,
                                gint           year,
                                gint           month,
                                gint           day,
                                GrlRectangle  *bounds)
{
	GDate  d;
	gint   cell;
	gfloat gx;
	gfloat gy;
	gfloat c;

	g_return_val_if_fail (LRG_IS_DATE_PICKER (self), FALSE);
	g_return_val_if_fail (bounds != NULL, FALSE);

	if (self->popup == NULL || !g_date_valid_dmy ((GDateDay) day, (GDateMonth) month, (GDateYear) year))
		return FALSE;
	g_date_clear (&d, 1);
	g_date_set_dmy (&d, (GDateDay) day, (GDateMonth) month, (GDateYear) year);
	cell = cell_of (self, &d);
	if (cell < 0)
		return FALSE;

	c = cal_cell_size (LRG_WIDGET (self->popup));
	cal_grid_origin (LRG_WIDGET (self->popup), &gx, &gy);
	bounds->x = gx + c * (gfloat) (cell % CAL_COLS);
	bounds->y = gy + c * (gfloat) (cell / CAL_COLS);
	bounds->width = c;
	bounds->height = c;
	return TRUE;
}
