/* lrg-gradient-edit.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGradientStops: a sorted list of colour stops.
 * LrgGradientEdit: a gradient bar with draggable stop handles.
 *
 * Layout: the gradient bar (BOX part look for its frame) over a
 * checker pattern so alpha shows, and below it a strip of handles,
 * one per stop: a small tab filled with the stop colour (HANDLE part),
 * the selected one outlined in the accent colour.
 *
 * Every change is bracketed by edit-begin / edit-end, so a property
 * grid can turn a whole drag into one undo step: a drag emits
 * edit-begin on the first movement, stops-changed on every frame and
 * edit-end (commit TRUE) on release, or edit-end (commit FALSE) after
 * putting the stops back when CANCEL (Escape) arrives mid-drag. Clicks
 * that add or delete a stop, and keyboard nudges, are one-shot
 * gestures. The colour picker popup is one gesture from its first
 * change until it closes.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lrg-gradient-edit.h"
#include "lrg-canvas.h"
#include "lrg-color-picker.h"
#include "lrg-style.h"
#include "../lrg-log.h"

/* Width and height of a stop handle */
#define HANDLE_W (10.0f)
#define HANDLE_H (12.0f)

/* Vertical travel below the bar after which a dragged stop is removed */
#define REMOVE_DISTANCE (28.0f)

/* Side of a checker square behind the bar */
#define CHECKER (6.0f)

/* Horizontal pointer travel before a press on a handle becomes a drag */
#define DRAG_THRESHOLD (2.0f)

/* Offset step of Left/Right on the selected stop */
#define NUDGE (0.01f)

/* ==========================================================================
 * LrgGradientStops
 * ========================================================================== */

typedef struct
{
	gfloat   offset;
	GrlColor color;
} Stop;

struct _LrgGradientStops
{
	GArray *stops;  /* of Stop, sorted by offset */
};

G_DEFINE_BOXED_TYPE (LrgGradientStops, lrg_gradient_stops, lrg_gradient_stops_copy, lrg_gradient_stops_free)

LrgGradientStops *
lrg_gradient_stops_new (void)
{
	LrgGradientStops *self = g_new0 (LrgGradientStops, 1);

	self->stops = g_array_new (FALSE, TRUE, sizeof (Stop));
	return self;
}

LrgGradientStops *
lrg_gradient_stops_new_two (const GrlColor *start,
                            const GrlColor *end)
{
	LrgGradientStops *self;

	g_return_val_if_fail (start != NULL && end != NULL, NULL);

	self = lrg_gradient_stops_new ();
	lrg_gradient_stops_add (self, 0.0f, start);
	lrg_gradient_stops_add (self, 1.0f, end);
	return self;
}

LrgGradientStops *
lrg_gradient_stops_new_from_string (const gchar *text)
{
	g_autoptr(LrgGradientStops) self = NULL;
	g_auto(GStrv)               parts = NULL;
	guint                       i;

	g_return_val_if_fail (text != NULL, NULL);

	/* "offset colour" pairs separated by ';' (empty pieces skipped) */
	self = lrg_gradient_stops_new ();
	parts = g_strsplit (text, ";", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		g_auto(GStrv) fields = NULL;
		gchar        *piece = g_strstrip (parts[i]);
		gchar        *end = NULL;
		gdouble       offset;
		GrlColor      color;

		if (*piece == '\0')
			continue;
		fields = g_strsplit_set (piece, " \t", 2);
		if (fields[0] == NULL || fields[1] == NULL)
			return NULL;
		offset = g_ascii_strtod (fields[0], &end);
		if (end == fields[0] || *end != '\0' || !isfinite (offset))
			return NULL;
		if (!lrg_color_parse (g_strstrip (fields[1]), &color))
			return NULL;
		lrg_gradient_stops_add (self, (gfloat) offset, &color);
	}
	return g_steal_pointer (&self);
}

LrgGradientStops *
lrg_gradient_stops_copy (const LrgGradientStops *self)
{
	LrgGradientStops *copy;

	g_return_val_if_fail (self != NULL, NULL);

	copy = lrg_gradient_stops_new ();
	g_array_append_vals (copy->stops, self->stops->data, self->stops->len);
	return copy;
}

void
lrg_gradient_stops_free (LrgGradientStops *self)
{
	if (self == NULL)
		return;
	g_array_unref (self->stops);
	g_free (self);
}

gboolean
lrg_gradient_stops_equal (const LrgGradientStops *a,
                          const LrgGradientStops *b)
{
	guint i;

	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL || a->stops->len != b->stops->len)
		return FALSE;

	for (i = 0; i < a->stops->len; i++)
	{
		const Stop *sa = &g_array_index (a->stops, Stop, i);
		const Stop *sb = &g_array_index (b->stops, Stop, i);

		if (fabsf (sa->offset - sb->offset) > 1e-6f ||
		    sa->color.r != sb->color.r || sa->color.g != sb->color.g ||
		    sa->color.b != sb->color.b || sa->color.a != sb->color.a)
			return FALSE;
	}
	return TRUE;
}

guint
lrg_gradient_stops_get_n_stops (const LrgGradientStops *self)
{
	g_return_val_if_fail (self != NULL, 0);
	return self->stops->len;
}

gboolean
lrg_gradient_stops_get_stop (const LrgGradientStops *self,
                             guint                   index,
                             gfloat                 *offset,
                             GrlColor               *color)
{
	const Stop *s;

	g_return_val_if_fail (self != NULL, FALSE);

	if (index >= self->stops->len)
		return FALSE;
	s = &g_array_index (self->stops, Stop, index);
	if (offset != NULL)
		*offset = s->offset;
	if (color != NULL)
		*color = s->color;
	return TRUE;
}

/* insert_sorted: after every stop whose offset is <= @stop's */
static guint
insert_sorted (LrgGradientStops *self,
               const Stop       *stop)
{
	guint i = 0;

	while (i < self->stops->len && g_array_index (self->stops, Stop, i).offset <= stop->offset)
		i++;
	g_array_insert_val (self->stops, i, *stop);
	return i;
}

guint
lrg_gradient_stops_add (LrgGradientStops *self,
                        gfloat            offset,
                        const GrlColor   *color)
{
	Stop s;

	g_return_val_if_fail (self != NULL, 0);
	g_return_val_if_fail (color != NULL, 0);

	s.offset = isfinite (offset) ? CLAMP (offset, 0.0f, 1.0f) : 0.0f;
	s.color = *color;
	return insert_sorted (self, &s);
}

gint
lrg_gradient_stops_set_stop (LrgGradientStops *self,
                             guint             index,
                             gfloat            offset,
                             const GrlColor   *color)
{
	Stop s;

	g_return_val_if_fail (self != NULL, -1);

	if (index >= self->stops->len)
		return -1;

	/* Take it out and put it back where its new offset sorts it */
	s = g_array_index (self->stops, Stop, index);
	s.offset = isfinite (offset) ? CLAMP (offset, 0.0f, 1.0f) : s.offset;
	if (color != NULL)
		s.color = *color;
	g_array_remove_index (self->stops, index);
	return (gint) insert_sorted (self, &s);
}

gboolean
lrg_gradient_stops_remove (LrgGradientStops *self,
                           guint             index)
{
	g_return_val_if_fail (self != NULL, FALSE);

	if (index >= self->stops->len)
		return FALSE;
	g_array_remove_index (self->stops, index);
	return TRUE;
}

/* mix_channel: a + (b - a) * f, rounded */
static guint8
mix_channel (guint8 a,
             guint8 b,
             gfloat f)
{
	return (guint8) CLAMP (roundf ((gfloat) a + ((gfloat) b - (gfloat) a) * f), 0.0f, 255.0f);
}

void
lrg_gradient_stops_evaluate (const LrgGradientStops *self,
                             gfloat                  t,
                             GrlColor               *color)
{
	const Stop *first;
	const Stop *last;
	guint       i;

	g_return_if_fail (self != NULL);
	g_return_if_fail (color != NULL);

	if (self->stops->len == 0)
	{
		color->r = color->g = color->b = color->a = 0;
		return;
	}
	t = isfinite (t) ? CLAMP (t, 0.0f, 1.0f) : 0.0f;

	/* Outside the stops: the nearest end colour */
	first = &g_array_index (self->stops, Stop, 0);
	last = &g_array_index (self->stops, Stop, self->stops->len - 1);
	if (t <= first->offset)
	{
		*color = first->color;
		return;
	}
	if (t >= last->offset)
	{
		*color = last->color;
		return;
	}

	/* Between two neighbours: linear blend of all four channels */
	for (i = 1; i < self->stops->len; i++)
	{
		const Stop *a = &g_array_index (self->stops, Stop, i - 1);
		const Stop *b = &g_array_index (self->stops, Stop, i);

		if (t <= b->offset)
		{
			gfloat span = b->offset - a->offset;
			gfloat f = span > 1e-6f ? (t - a->offset) / span : 1.0f;

			color->r = mix_channel (a->color.r, b->color.r, f);
			color->g = mix_channel (a->color.g, b->color.g, f);
			color->b = mix_channel (a->color.b, b->color.b, f);
			color->a = mix_channel (a->color.a, b->color.a, f);
			return;
		}
	}
	*color = last->color;
}

gchar *
lrg_gradient_stops_to_string (const LrgGradientStops *self)
{
	GString *out;
	guint    i;

	g_return_val_if_fail (self != NULL, NULL);

	out = g_string_new (NULL);
	for (i = 0; i < self->stops->len; i++)
	{
		const Stop *s = &g_array_index (self->stops, Stop, i);
		gchar       num[G_ASCII_DTOSTR_BUF_SIZE];

		g_ascii_formatd (num, sizeof (num), "%.4g", (gdouble) s->offset);
		g_string_append_printf (out, "%s%s #%02x%02x%02x%02x", i > 0 ? "; " : "", num,
		                        s->color.r, s->color.g, s->color.b, s->color.a);
	}
	return g_string_free (out, FALSE);
}

/* ==========================================================================
 * LrgGradientEdit
 * ========================================================================== */

struct _LrgGradientEdit
{
	LrgWidget          parent_instance;

	LrgGradientStops  *stops;
	gint               selected;

	/* Gesture: a handle drag or the picker popup */
	gboolean           in_gesture;
	LrgGradientStops  *original;     /* stops when the gesture began */
	gboolean           pressing;     /* left button down on a handle */
	gboolean           dragging;     /* moved past the threshold */
	gboolean           removing;     /* dragged far below: removed on release */
	gboolean           was_selected; /* the pressed handle was already selected */
	gfloat             press_x;
	gfloat             press_y;
	gfloat             grab_dx;      /* pointer x - handle centre at the press */

	/* Colour picker popup for the selected stop */
	LrgColorPicker    *picker;
	LrgCanvas         *picker_canvas;
	gulong             dismiss_id;
};

G_DEFINE_FINAL_TYPE (LrgGradientEdit, lrg_gradient_edit, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_STOPS,
	PROP_SELECTED,
	N_PROPS
};

enum
{
	SIGNAL_EDIT_BEGIN,
	SIGNAL_STOPS_CHANGED,
	SIGNAL_EDIT_END,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Geometry
 * ========================================================================== */

/* bar_height: the gradient bar, a control height minus the handle strip */
static gfloat
bar_height (LrgGradientEdit *self)
{
	gfloat ch = lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
	                                  LRG_THEME_METRIC_CONTROL_HEIGHT);

	return MAX (8.0f, ch - 8.0f);
}

void
lrg_gradient_edit_get_bar_rect (LrgGradientEdit *self,
                                GrlRectangle    *rect)
{
	LrgWidget *w = LRG_WIDGET (self);

	g_return_if_fail (LRG_IS_GRADIENT_EDIT (self));
	g_return_if_fail (rect != NULL);

	/* Inset by half a handle so the end handles stay inside */
	rect->x = lrg_widget_get_world_x (w) + HANDLE_W * 0.5f;
	rect->y = lrg_widget_get_world_y (w);
	rect->width = MAX (1.0f, lrg_widget_get_width (w) - HANDLE_W);
	rect->height = bar_height (self);
}

gboolean
lrg_gradient_edit_get_handle_rect (LrgGradientEdit *self,
                                   guint            index,
                                   GrlRectangle    *rect)
{
	GrlRectangle bar;
	gfloat       offset;

	g_return_val_if_fail (LRG_IS_GRADIENT_EDIT (self), FALSE);
	g_return_val_if_fail (rect != NULL, FALSE);

	if (!lrg_gradient_stops_get_stop (self->stops, index, &offset, NULL))
		return FALSE;
	lrg_gradient_edit_get_bar_rect (self, &bar);
	rect->x = bar.x + offset * bar.width - HANDLE_W * 0.5f;
	rect->y = bar.y + bar.height + 1.0f;
	rect->width = HANDLE_W;
	rect->height = HANDLE_H;
	return TRUE;
}

/* handle_at: the stop whose handle contains (@px, @py), topmost
 * (selected first, then the last drawn), or -1 */
static gint
handle_at (LrgGradientEdit *self,
           gfloat           px,
           gfloat           py)
{
	GrlRectangle r;
	gint         i;

	if (self->selected >= 0 && lrg_gradient_edit_get_handle_rect (self, (guint) self->selected, &r) &&
	    px >= r.x - 2.0f && px <= r.x + r.width + 2.0f && py >= r.y - 2.0f && py <= r.y + r.height + 2.0f)
		return self->selected;

	for (i = (gint) lrg_gradient_stops_get_n_stops (self->stops) - 1; i >= 0; i--)
	{
		if (lrg_gradient_edit_get_handle_rect (self, (guint) i, &r) &&
		    px >= r.x - 2.0f && px <= r.x + r.width + 2.0f && py >= r.y - 2.0f && py <= r.y + r.height + 2.0f)
			return i;
	}
	return -1;
}

/* offset_at: bar position of world X @px, 0..1 */
static gfloat
offset_at (LrgGradientEdit *self,
           gfloat           px)
{
	GrlRectangle bar;

	lrg_gradient_edit_get_bar_rect (self, &bar);
	return CLAMP ((px - bar.x) / bar.width, 0.0f, 1.0f);
}

/* ==========================================================================
 * Gestures
 * ========================================================================== */

/* begin_gesture: remember the stops and announce the gesture once */
static void
begin_gesture (LrgGradientEdit *self)
{
	if (self->in_gesture)
		return;
	self->in_gesture = TRUE;
	lrg_gradient_stops_free (self->original);
	self->original = lrg_gradient_stops_copy (self->stops);
	g_signal_emit (self, signals[SIGNAL_EDIT_BEGIN], 0);
}

/* changed: the stops moved; repaint and tell listeners */
static void
changed (LrgGradientEdit *self)
{
	g_signal_emit (self, signals[SIGNAL_STOPS_CHANGED], 0);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STOPS]);
}

/* end_gesture: close the gesture; on cancel restore the stops first */
static void
end_gesture (LrgGradientEdit *self,
             gboolean         commit)
{
	if (!self->in_gesture)
		return;
	self->in_gesture = FALSE;
	if (!commit && self->original != NULL)
	{
		lrg_gradient_stops_free (self->stops);
		self->stops = g_steal_pointer (&self->original);
		if (self->selected >= (gint) lrg_gradient_stops_get_n_stops (self->stops))
			self->selected = -1;
		changed (self);
	}
	g_clear_pointer (&self->original, lrg_gradient_stops_free);
	g_signal_emit (self, signals[SIGNAL_EDIT_END], 0, commit);
}

/* select: change the selected stop, notifying */
static void
select_stop (LrgGradientEdit *self,
             gint             index)
{
	if (self->selected == index)
		return;
	self->selected = index;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTED]);
}

/* ==========================================================================
 * Colour picker popup
 * ========================================================================== */

static void
on_picker_color_changed (LrgColorPicker *picker,
                         gpointer        user_data)
{
	LrgGradientEdit *self = LRG_GRADIENT_EDIT (user_data);
	GrlColor         color;
	gfloat           offset;
	gint             index;

	if (self->selected < 0 ||
	    !lrg_gradient_stops_get_stop (self->stops, (guint) self->selected, &offset, NULL))
		return;

	/* The whole picker session is one gesture, ended when it closes */
	begin_gesture (self);
	lrg_color_picker_get_color (picker, &color);
	index = lrg_gradient_stops_set_stop (self->stops, (guint) self->selected, offset, &color);
	select_stop (self, index);
	changed (self);
}

/* close_picker_session: forget the picker; commit its gesture */
static void
close_picker_session (LrgGradientEdit *self)
{
	if (self->picker_canvas != NULL && self->dismiss_id != 0)
		g_signal_handler_disconnect (self->picker_canvas, self->dismiss_id);
	self->dismiss_id = 0;
	g_clear_weak_pointer (&self->picker_canvas);
	if (self->picker != NULL)
		g_signal_handlers_disconnect_by_data (self->picker, self);
	g_clear_object (&self->picker);
	if (!self->pressing)
		end_gesture (self, TRUE);
}

static void
on_popup_dismissed (LrgCanvas *canvas,
                    LrgWidget *popup,
                    gpointer   user_data)
{
	LrgGradientEdit *self = LRG_GRADIENT_EDIT (user_data);

	if (self->picker != NULL && popup == LRG_WIDGET (self->picker))
		close_picker_session (self);
}

void
lrg_gradient_edit_open_color_picker (LrgGradientEdit *self)
{
	LrgWidget *root;
	GrlColor   color;

	g_return_if_fail (LRG_IS_GRADIENT_EDIT (self));

	root = lrg_widget_get_root (LRG_WIDGET (self));
	if (!LRG_IS_CANVAS (root) || self->selected < 0 || self->picker != NULL)
		return;
	if (!lrg_gradient_stops_get_stop (self->stops, (guint) self->selected, NULL, &color))
		return;

	self->picker = lrg_color_picker_new ();
	lrg_color_picker_set_show_alpha (self->picker, TRUE);
	lrg_color_picker_set_color (self->picker, &color);
	g_signal_connect (self->picker, "color-changed", G_CALLBACK (on_picker_color_changed), self);
	g_set_weak_pointer (&self->picker_canvas, LRG_CANVAS (root));
	self->dismiss_id = g_signal_connect (root, "popup-dismissed", G_CALLBACK (on_popup_dismissed), self);
	lrg_canvas_show_popup (LRG_CANVAS (root), LRG_WIDGET (self->picker), LRG_WIDGET (self),
	                       LRG_POPUP_PLACEMENT_BELOW, FALSE);
}

LrgWidget *
lrg_gradient_edit_get_color_picker (LrgGradientEdit *self)
{
	g_return_val_if_fail (LRG_IS_GRADIENT_EDIT (self), NULL);
	return self->picker != NULL ? LRG_WIDGET (self->picker) : NULL;
}

/* ==========================================================================
 * Layout and painting
 * ========================================================================== */

static void
lrg_gradient_edit_measure (LrgWidget *widget,
                           gfloat    *preferred_width,
                           gfloat    *preferred_height)
{
	LrgGradientEdit *self = LRG_GRADIENT_EDIT (widget);

	*preferred_width = 160.0f;
	*preferred_height = bar_height (self) + HANDLE_H + 2.0f;
}

static void
lrg_gradient_edit_style_defaults (LrgWidget      *widget,
                                  LrgUIPart       part,
                                  LrgWidgetState  state,
                                  LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_BOX:
		/* Frame of the bar */
		lrg_part_look_set_border (look, lrg_theme_get_color (theme,
		                          (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_FOCUSED))
		                          ? LRG_THEME_COLOR_ACCENT : LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) * 0.5f);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_HANDLE:
		/* Stop tabs: outline in the text colour, accent when selected */
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 1.0f);
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_radius (look, 2.0f);
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_gradient_edit_parent_class)->style_defaults (widget, part, state, look);
}

/* paint_checker: two-tone squares so transparent stops read */
static void
paint_checker (LrgPaintList       *list,
               const GrlRectangle *r)
{
	GrlColor light = { 200, 200, 200, 255 };
	GrlColor dark = { 140, 140, 140, 255 };
	gfloat   cx;
	gfloat   cy;
	guint    col;
	guint    row;

	lrg_paint_list_rect (list, r->x, r->y, r->width, r->height, 0.0f, &light);
	for (row = 0, cy = r->y; cy < r->y + r->height; row++, cy += CHECKER)
		for (col = 0, cx = r->x; cx < r->x + r->width; col++, cx += CHECKER)
			if ((row + col) % 2 == 1)
				lrg_paint_list_rect (list, cx, cy, MIN (CHECKER, r->x + r->width - cx),
				                     MIN (CHECKER, r->y + r->height - cy), 0.0f, &dark);
}

/* paint_gradient_bar: solid ends plus one linear gradient per pair */
static void
paint_gradient_bar (const LrgGradientStops *stops,
                    LrgPaintList           *list,
                    const GrlRectangle     *bar)
{
	guint    n = lrg_gradient_stops_get_n_stops (stops);
	guint    i;
	gfloat   o0 = 0.0f;
	gfloat   o1 = 1.0f;
	GrlColor c0 = { 0, 0, 0, 0 };
	GrlColor c1 = { 0, 0, 0, 0 };

	if (n == 0)
		return;

	lrg_gradient_stops_get_stop (stops, 0, &o0, &c0);
	if (o0 > 0.0f)
		lrg_paint_list_rect (list, bar->x, bar->y, bar->width * o0, bar->height, 0.0f, &c0);
	for (i = 1; i < n; i++)
	{
		LrgPaintGradient g = { 0 };

		lrg_gradient_stops_get_stop (stops, i - 1, &o0, &c0);
		lrg_gradient_stops_get_stop (stops, i, &o1, &c1);
		if (o1 - o0 <= 0.0001f)
			continue;
		g.kind = LRG_PAINT_GRADIENT_LINEAR;
		g.start = c0;
		g.end = c1;
		g.angle = 0.0f;
		lrg_paint_list_rect_ex (list, bar->x + bar->width * o0, bar->y, bar->width * (o1 - o0), bar->height,
		                        NULL, &c0, &g, 0.0f, NULL);
	}
	lrg_gradient_stops_get_stop (stops, n - 1, &o1, &c1);
	if (o1 < 1.0f)
		lrg_paint_list_rect (list, bar->x + bar->width * o1, bar->y, bar->width * (1.0f - o1), bar->height,
		                     0.0f, &c1);
}

static void
lrg_gradient_edit_paint (LrgWidget    *widget,
                         LrgPaintList *list)
{
	LrgGradientEdit *self = LRG_GRADIENT_EDIT (widget);
	LrgPartLook      box;
	LrgPartLook      handle;
	GrlRectangle     bar;
	gboolean         faded;
	guint            n = lrg_gradient_stops_get_n_stops (self->stops);
	guint            i;

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	faded = box.opacity < 1.0f;
	if (faded)
		lrg_paint_list_push_opacity (list, box.opacity);

	/* 1. Bar: checker, gradient, frame */
	lrg_gradient_edit_get_bar_rect (self, &bar);
	lrg_paint_list_push_clip (list, bar.x, bar.y, bar.width, bar.height);
	paint_checker (list, &bar);
	paint_gradient_bar (self->stops, list, &bar);
	lrg_paint_list_pop_clip (list);
	lrg_paint_list_border (list, bar.x, bar.y, bar.width, bar.height, box.radii.top_left,
	                       MAX (1.0f, box.border_width), &box.border);

	/* 2. Handles, the selected one last (on top) and in the accent */
	lrg_widget_resolve_look (widget, LRG_UI_PART_HANDLE, &handle);
	for (i = 0; i <= n; i++)
	{
		GrlRectangle r;
		GrlColor     color;
		gboolean     sel;
		guint        index;
		gfloat       cx;

		/* Pass n paints the selected handle */
		if (i == n)
		{
			if (self->selected < 0)
				break;
			index = (guint) self->selected;
		}
		else
		{
			if ((gint) i == self->selected)
				continue;
			index = i;
		}
		sel = (gint) index == self->selected;
		if (!lrg_gradient_edit_get_handle_rect (self, index, &r))
			continue;
		lrg_gradient_stops_get_stop (self->stops, index, NULL, &color);
		cx = r.x + r.width * 0.5f;

		/* A stop about to be removed is drawn faded */
		if (sel && self->removing)
			lrg_paint_list_push_opacity (list, 0.35f);
		lrg_paint_list_triangle (list, cx, r.y - 1.0f, cx - 3.0f, r.y + 3.0f, cx + 3.0f, r.y + 3.0f,
		                         sel ? &handle.foreground : &handle.border);
		lrg_paint_list_rect (list, r.x, r.y + 3.0f, r.width, r.height - 3.0f, 2.0f, &color);
		lrg_paint_list_border (list, r.x, r.y + 3.0f, r.width, r.height - 3.0f, 2.0f, sel ? 2.0f : 1.0f,
		                       sel ? &handle.foreground : &handle.border);
		if (sel && self->removing)
			lrg_paint_list_pop_opacity (list);
	}

	if (faded)
		lrg_paint_list_pop_opacity (list);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* one_shot_add: a click on the bar adds a stop with the colour there */
static void
one_shot_add (LrgGradientEdit *self,
              gfloat           px)
{
	GrlColor color;
	gfloat   t = offset_at (self, px);
	guint    index;

	lrg_gradient_stops_evaluate (self->stops, t, &color);
	begin_gesture (self);
	index = lrg_gradient_stops_add (self->stops, t, &color);
	select_stop (self, (gint) index);
	changed (self);
	end_gesture (self, TRUE);
}

/* one_shot_remove: delete the selected stop while more than two remain */
static gboolean
one_shot_remove (LrgGradientEdit *self)
{
	if (self->selected < 0 || lrg_gradient_stops_get_n_stops (self->stops) <= 2)
		return FALSE;
	begin_gesture (self);
	lrg_gradient_stops_remove (self->stops, (guint) self->selected);
	select_stop (self, -1);
	changed (self);
	end_gesture (self, TRUE);
	return TRUE;
}

/* one_shot_nudge: move the selected stop by @delta */
static gboolean
one_shot_nudge (LrgGradientEdit *self,
                gfloat           delta)
{
	gfloat offset;
	gint   index;

	if (self->selected < 0 ||
	    !lrg_gradient_stops_get_stop (self->stops, (guint) self->selected, &offset, NULL))
		return FALSE;
	begin_gesture (self);
	index = lrg_gradient_stops_set_stop (self->stops, (guint) self->selected, offset + delta, NULL);
	select_stop (self, index);
	changed (self);
	end_gesture (self, TRUE);
	return TRUE;
}

static gboolean
lrg_gradient_edit_handle_event (LrgWidget        *widget,
                                const LrgUIEvent *event)
{
	LrgGradientEdit *self = LRG_GRADIENT_EDIT (widget);
	gfloat           px = lrg_ui_event_get_x (event);
	gfloat           py = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			GrlRectangle bar;
			gint         hit;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			hit = handle_at (self, px, py);
			if (hit >= 0)
			{
				GrlRectangle r;

				/* Press on a handle: select it, a drag may follow */
				self->was_selected = hit == self->selected;
				select_stop (self, hit);
				lrg_gradient_edit_get_handle_rect (self, (guint) hit, &r);
				self->pressing = TRUE;
				self->dragging = FALSE;
				self->removing = FALSE;
				self->press_x = px;
				self->press_y = py;
				self->grab_dx = px - (r.x + r.width * 0.5f);
				return TRUE;
			}

			/* Press on the bar: a new stop */
			lrg_gradient_edit_get_bar_rect (self, &bar);
			if (py >= bar.y && py <= bar.y + bar.height)
			{
				one_shot_add (self, px);
				return TRUE;
			}
			return FALSE;
		}

	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->pressing && self->selected >= 0)
		{
			GrlRectangle bar;
			GrlColor     color;
			gint         index;

			if (!self->dragging && fabsf (px - self->press_x) < DRAG_THRESHOLD &&
			    fabsf (py - self->press_y) < DRAG_THRESHOLD)
				return TRUE;

			/* First movement opens the gesture */
			if (!self->dragging)
			{
				self->dragging = TRUE;
				begin_gesture (self);
			}

			/* Far below the bar: marked for removal on release */
			lrg_gradient_edit_get_bar_rect (self, &bar);
			self->removing = lrg_gradient_stops_get_n_stops (self->stops) > 2 &&
			                 py > bar.y + bar.height + HANDLE_H + REMOVE_DISTANCE;

			lrg_gradient_stops_get_stop (self->stops, (guint) self->selected, NULL, &color);
			index = lrg_gradient_stops_set_stop (self->stops, (guint) self->selected,
			                                     offset_at (self, px - self->grab_dx), &color);
			select_stop (self, index);
			changed (self);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0 || !self->pressing)
			return FALSE;
		self->pressing = FALSE;
		if (self->dragging)
		{
			/* Dropped far below: the stop goes, still in this gesture */
			if (self->removing && self->selected >= 0)
			{
				lrg_gradient_stops_remove (self->stops, (guint) self->selected);
				select_stop (self, -1);
				changed (self);
			}
			self->removing = FALSE;
			self->dragging = FALSE;
			end_gesture (self, TRUE);
		}
		else if (self->was_selected)
		{
			/* A second click on the selected stop edits its colour */
			lrg_gradient_edit_open_color_picker (self);
		}
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		/* Escape mid-drag: put everything back */
		if (self->pressing && self->dragging)
		{
			self->pressing = FALSE;
			self->dragging = FALSE;
			self->removing = FALSE;
			end_gesture (self, FALSE);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_ACTIVATE:
		if (self->selected >= 0)
		{
			lrg_gradient_edit_open_color_picker (self);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_DELETE:
		case GRL_KEY_BACKSPACE:
			return one_shot_remove (self);
		case GRL_KEY_LEFT:
			return one_shot_nudge (self, -NUDGE);
		case GRL_KEY_RIGHT:
			return one_shot_nudge (self, NUDGE);
		default:
			return FALSE;
		}

	case LRG_UI_EVENT_FOCUS_OUT:
		/* Losing focus mid-drag commits what is there */
		if (self->pressing && self->dragging && self->picker == NULL)
		{
			self->pressing = FALSE;
			self->dragging = FALSE;
			end_gesture (self, TRUE);
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
lrg_gradient_edit_dispose (GObject *object)
{
	LrgGradientEdit *self = LRG_GRADIENT_EDIT (object);

	if (self->picker != NULL)
	{
		LrgCanvas *canvas = self->picker_canvas;
		LrgWidget *picker = LRG_WIDGET (g_object_ref (self->picker));

		close_picker_session (self);
		if (canvas != NULL)
			lrg_canvas_dismiss_popup (canvas, picker);
		g_object_unref (picker);
	}
	G_OBJECT_CLASS (lrg_gradient_edit_parent_class)->dispose (object);
}

static void
lrg_gradient_edit_finalize (GObject *object)
{
	LrgGradientEdit *self = LRG_GRADIENT_EDIT (object);

	lrg_gradient_stops_free (self->stops);
	lrg_gradient_stops_free (self->original);
	G_OBJECT_CLASS (lrg_gradient_edit_parent_class)->finalize (object);
}

static void
lrg_gradient_edit_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgGradientEdit *self = LRG_GRADIENT_EDIT (object);

	switch (prop_id)
	{
	case PROP_STOPS:
		g_value_set_boxed (value, self->stops);
		break;
	case PROP_SELECTED:
		g_value_set_int (value, self->selected);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_gradient_edit_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgGradientEdit *self = LRG_GRADIENT_EDIT (object);

	switch (prop_id)
	{
	case PROP_STOPS:
		lrg_gradient_edit_set_stops (self, g_value_get_boxed (value));
		break;
	case PROP_SELECTED:
		lrg_gradient_edit_set_selected (self, g_value_get_int (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_gradient_edit_class_init (LrgGradientEditClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_gradient_edit_dispose;
	object_class->finalize = lrg_gradient_edit_finalize;
	object_class->get_property = lrg_gradient_edit_get_property;
	object_class->set_property = lrg_gradient_edit_set_property;

	widget_class->measure = lrg_gradient_edit_measure;
	widget_class->paint = lrg_gradient_edit_paint;
	widget_class->style_defaults = lrg_gradient_edit_style_defaults;
	widget_class->handle_event = lrg_gradient_edit_handle_event;

	/**
	 * LrgGradientEdit:stops:
	 *
	 * The edited #LrgGradientStops.
	 */
	properties[PROP_STOPS] =
		g_param_spec_boxed ("stops", NULL, NULL, LRG_TYPE_GRADIENT_STOPS,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgGradientEdit:selected:
	 *
	 * The selected stop index, -1 for none.
	 */
	properties[PROP_SELECTED] =
		g_param_spec_int ("selected", NULL, NULL, -1, G_MAXINT, -1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgGradientEdit::edit-begin:
	 * @self: the editor
	 *
	 * A gesture starts: a drag moved, a click is about to add or
	 * delete a stop, or the colour picker made its first change.
	 */
	signals[SIGNAL_EDIT_BEGIN] =
		g_signal_new ("edit-begin", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgGradientEdit::stops-changed:
	 * @self: the editor
	 *
	 * The stops changed, always between #LrgGradientEdit::edit-begin
	 * and #LrgGradientEdit::edit-end (every frame of a drag).
	 */
	signals[SIGNAL_STOPS_CHANGED] =
		g_signal_new ("stops-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgGradientEdit::edit-end:
	 * @self: the editor
	 * @commit: %FALSE when the gesture was cancelled (Escape) and the
	 *   stops were put back
	 *
	 * The gesture ended.
	 */
	signals[SIGNAL_EDIT_END] =
		g_signal_new ("edit-end", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
lrg_gradient_edit_init (LrgGradientEdit *self)
{
	GrlColor black = { 0, 0, 0, 255 };
	GrlColor white = { 255, 255, 255, 255 };

	self->stops = lrg_gradient_stops_new_two (&black, &white);
	self->selected = -1;
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgGradientEdit *
lrg_gradient_edit_new (void)
{
	return g_object_new (LRG_TYPE_GRADIENT_EDIT, NULL);
}

const LrgGradientStops *
lrg_gradient_edit_get_stops (LrgGradientEdit *self)
{
	g_return_val_if_fail (LRG_IS_GRADIENT_EDIT (self), NULL);
	return self->stops;
}

void
lrg_gradient_edit_set_stops (LrgGradientEdit        *self,
                             const LrgGradientStops *stops)
{
	GrlColor black = { 0, 0, 0, 255 };
	GrlColor white = { 255, 255, 255, 255 };

	g_return_if_fail (LRG_IS_GRADIENT_EDIT (self));

	if (stops != NULL && lrg_gradient_stops_equal (stops, self->stops))
		return;
	lrg_gradient_stops_free (self->stops);
	self->stops = stops != NULL ? lrg_gradient_stops_copy (stops) : lrg_gradient_stops_new_two (&black, &white);
	if (self->selected >= (gint) lrg_gradient_stops_get_n_stops (self->stops))
		select_stop (self, -1);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STOPS]);
}

gint
lrg_gradient_edit_get_selected (LrgGradientEdit *self)
{
	g_return_val_if_fail (LRG_IS_GRADIENT_EDIT (self), -1);
	return self->selected;
}

void
lrg_gradient_edit_set_selected (LrgGradientEdit *self,
                                gint             index)
{
	g_return_if_fail (LRG_IS_GRADIENT_EDIT (self));

	if (index < -1 || index >= (gint) lrg_gradient_stops_get_n_stops (self->stops))
		index = -1;
	select_stop (self, index);
}
