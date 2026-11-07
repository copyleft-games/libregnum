/* lrg-curve-edit.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCurveEdit: an LrgKeyframeCurve preview and editor.
 *
 * Two modes share one type:
 *
 *  - compact (the default): an INPUT-coloured box with the curve
 *    sampled into a polyline. A click or ACTIVATE opens a second
 *    LrgCurveEdit in full mode as a popup below it; the popup edits a
 *    copy, and every change is mirrored back so the compact editor's
 *    curve-changed fires live. The whole popup session is one gesture
 *    (edit-begin at its first change, edit-end when it closes).
 *
 *  - full: a graph with a light grid, the curve, and a dot per key.
 *    Drag a key (time is clamped between its neighbours so the order
 *    never changes), click empty space to add a key on the curve,
 *    Delete removes the selected key, the wheel or E/Shift+E cycle the
 *    selected key's easing. A drag is one gesture: edit-begin on the
 *    first movement, curve-changed every frame, edit-end on release;
 *    CANCEL (Escape) mid-drag restores the keys and ends with commit
 *    FALSE.
 *
 * LrgKeyframeCurve has no tangents: each key carries the easing used
 * towards the next key, which is what the editor exposes.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-curve-edit.h"
#include "lrg-canvas.h"
#include "../lrg-log.h"

/* Size of the full editor */
#define FULL_WIDTH  (320.0f)
#define FULL_HEIGHT (200.0f)

/* Inner padding of the graph, and the strip for the easing label */
#define GRAPH_PAD   (10.0f)
#define FOOTER_H    (18.0f)

/* Radius of a key dot and its hit radius */
#define KEY_RADIUS  (4.0f)
#define KEY_HIT     (8.0f)

/* Line segments used to draw the curve */
#define SAMPLES     (64)

/* Minimum time gap kept between neighbouring keys */
#define MIN_GAP     (0.001f)

/* Pointer travel before a press on a key becomes a drag */
#define DRAG_THRESHOLD (2.0f)

typedef struct
{
	gfloat        t;
	gfloat        value;
	LrgEasingType easing;
} Key;

struct _LrgCurveEdit
{
	LrgWidget         parent_instance;

	LrgKeyframeCurve *curve;
	gboolean          compact;
	gint              selected;
	gfloat            range_min;
	gfloat            range_max;

	/* Gesture */
	gboolean          in_gesture;
	LrgKeyframeCurve *original;
	gboolean          pressing;
	gboolean          dragging;
	gfloat            press_x;
	gfloat            press_y;

	/* Compact mode: the popup editor */
	LrgCurveEdit     *popup;
	LrgCanvas        *popup_canvas;
	gulong            dismiss_id;
};

G_DEFINE_FINAL_TYPE (LrgCurveEdit, lrg_curve_edit, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_CURVE,
	PROP_COMPACT,
	PROP_SELECTED,
	N_PROPS
};

enum
{
	SIGNAL_EDIT_BEGIN,
	SIGNAL_CURVE_CHANGED,
	SIGNAL_EDIT_END,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Keys
 * ========================================================================== */

/* read_keys: the curve's keys, in time order */
static GArray *
read_keys (LrgKeyframeCurve *curve)
{
	GArray *keys = g_array_new (FALSE, TRUE, sizeof (Key));
	guint   n = lrg_keyframe_curve_get_key_count (curve);
	guint   i;

	for (i = 0; i < n; i++)
	{
		Key k;

		if (lrg_keyframe_curve_get_key (curve, i, &k.t, &k.value, &k.easing))
			g_array_append_val (keys, k);
	}
	return keys;
}

/* write_keys: replace the curve's keys */
static void
write_keys (LrgKeyframeCurve *curve,
            GArray           *keys)
{
	guint i;

	lrg_keyframe_curve_clear (curve);
	for (i = 0; i < keys->len; i++)
	{
		Key *k = &g_array_index (keys, Key, i);

		lrg_keyframe_curve_add_key (curve, k->t, k->value, k->easing);
	}
}

/* default_curve: a linear ramp 0 -> 1 */
static LrgKeyframeCurve *
default_curve (void)
{
	LrgKeyframeCurve *curve = lrg_keyframe_curve_new ();

	lrg_keyframe_curve_add_key (curve, 0.0f, 0.0f, LRG_EASING_LINEAR);
	lrg_keyframe_curve_add_key (curve, 1.0f, 1.0f, LRG_EASING_LINEAR);
	return curve;
}

/* curves_equal: same keys */
static gboolean
curves_equal (LrgKeyframeCurve *a,
              LrgKeyframeCurve *b)
{
	guint n = lrg_keyframe_curve_get_key_count (a);
	guint i;

	if (n != lrg_keyframe_curve_get_key_count (b))
		return FALSE;
	for (i = 0; i < n; i++)
	{
		Key ka;
		Key kb;

		lrg_keyframe_curve_get_key (a, i, &ka.t, &ka.value, &ka.easing);
		lrg_keyframe_curve_get_key (b, i, &kb.t, &kb.value, &kb.easing);
		if (ka.t != kb.t || ka.value != kb.value || ka.easing != kb.easing)
			return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * Geometry
 * ========================================================================== */

/* time_range: 0..1 widened by keys outside it */
static void
time_range (LrgCurveEdit *self,
            gfloat       *t0,
            gfloat       *t1)
{
	gfloat start = 0.0f;
	gfloat end = 1.0f;

	if (lrg_keyframe_curve_get_time_range (self->curve, &start, &end))
	{
		start = MIN (start, 0.0f);
		end = MAX (end, 1.0f);
	}
	else
	{
		start = 0.0f;
		end = 1.0f;
	}
	*t0 = start;
	*t1 = end > start ? end : start + 1.0f;
}

void
lrg_curve_edit_get_value_range (LrgCurveEdit *self,
                                gfloat       *min,
                                gfloat       *max)
{
	gfloat lo;
	gfloat hi;
	guint  n;
	guint  i;

	g_return_if_fail (LRG_IS_CURVE_EDIT (self));

	/* The configured range, widened by keys and by eased overshoot */
	lo = self->range_min;
	hi = self->range_max;
	n = lrg_keyframe_curve_get_key_count (self->curve);
	for (i = 0; i < n; i++)
	{
		gfloat v;

		lrg_keyframe_curve_get_key (self->curve, i, NULL, &v, NULL);
		lo = MIN (lo, v);
		hi = MAX (hi, v);
	}
	if (hi - lo < 1e-4f)
		hi = lo + 1.0f;
	if (min != NULL)
		*min = lo;
	if (max != NULL)
		*max = hi;
}

void
lrg_curve_edit_get_graph_rect (LrgCurveEdit *self,
                               GrlRectangle *rect)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     pad = self->compact ? 4.0f : GRAPH_PAD;
	gfloat     footer = self->compact ? 0.0f : FOOTER_H;

	g_return_if_fail (LRG_IS_CURVE_EDIT (self));
	g_return_if_fail (rect != NULL);

	rect->x = lrg_widget_get_world_x (w) + pad;
	rect->y = lrg_widget_get_world_y (w) + pad;
	rect->width = MAX (1.0f, lrg_widget_get_width (w) - pad * 2.0f);
	rect->height = MAX (1.0f, lrg_widget_get_height (w) - pad * 2.0f - footer);
}

/* to_screen: (t, v) -> world point */
static void
to_screen (LrgCurveEdit *self,
           gfloat        t,
           gfloat        v,
           gfloat       *x,
           gfloat       *y)
{
	GrlRectangle g;
	gfloat       t0;
	gfloat       t1;
	gfloat       lo;
	gfloat       hi;

	lrg_curve_edit_get_graph_rect (self, &g);
	time_range (self, &t0, &t1);
	lrg_curve_edit_get_value_range (self, &lo, &hi);
	*x = g.x + (t - t0) / (t1 - t0) * g.width;
	*y = g.y + g.height - (v - lo) / (hi - lo) * g.height;
}

/* from_screen: world point -> (t, v) */
static void
from_screen (LrgCurveEdit *self,
             gfloat        x,
             gfloat        y,
             gfloat       *t,
             gfloat       *v)
{
	GrlRectangle g;
	gfloat       t0;
	gfloat       t1;
	gfloat       lo;
	gfloat       hi;

	lrg_curve_edit_get_graph_rect (self, &g);
	time_range (self, &t0, &t1);
	lrg_curve_edit_get_value_range (self, &lo, &hi);
	*t = t0 + (x - g.x) / g.width * (t1 - t0);
	*v = lo + (g.y + g.height - y) / g.height * (hi - lo);
}

gboolean
lrg_curve_edit_get_key_position (LrgCurveEdit *self,
                                 guint         index,
                                 gfloat       *x,
                                 gfloat       *y)
{
	gfloat t;
	gfloat v;
	gfloat sx;
	gfloat sy;

	g_return_val_if_fail (LRG_IS_CURVE_EDIT (self), FALSE);

	if (!lrg_keyframe_curve_get_key (self->curve, index, &t, &v, NULL))
		return FALSE;
	to_screen (self, t, v, &sx, &sy);
	if (x != NULL)
		*x = sx;
	if (y != NULL)
		*y = sy;
	return TRUE;
}

/* key_at: the key within KEY_HIT of (@px, @py), or -1 */
static gint
key_at (LrgCurveEdit *self,
        gfloat        px,
        gfloat        py)
{
	guint n = lrg_keyframe_curve_get_key_count (self->curve);
	gint  best = -1;
	gfloat best_d = KEY_HIT * KEY_HIT;
	guint i;

	for (i = 0; i < n; i++)
	{
		gfloat kx;
		gfloat ky;
		gfloat d;

		lrg_curve_edit_get_key_position (self, i, &kx, &ky);
		d = (kx - px) * (kx - px) + (ky - py) * (ky - py);
		if (d <= best_d)
		{
			best_d = d;
			best = (gint) i;
		}
	}
	return best;
}

/* ==========================================================================
 * Gestures
 * ========================================================================== */

static void
begin_gesture (LrgCurveEdit *self)
{
	if (self->in_gesture)
		return;
	self->in_gesture = TRUE;
	g_clear_object (&self->original);
	self->original = lrg_keyframe_curve_copy (self->curve);
	g_signal_emit (self, signals[SIGNAL_EDIT_BEGIN], 0);
}

static void
changed (LrgCurveEdit *self)
{
	g_signal_emit (self, signals[SIGNAL_CURVE_CHANGED], 0);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURVE]);
}

static void
end_gesture (LrgCurveEdit *self,
             gboolean      commit)
{
	if (!self->in_gesture)
		return;
	self->in_gesture = FALSE;
	if (!commit && self->original != NULL)
	{
		g_object_unref (self->curve);
		self->curve = g_steal_pointer (&self->original);
		if (self->selected >= (gint) lrg_keyframe_curve_get_key_count (self->curve))
			self->selected = -1;
		changed (self);
	}
	g_clear_object (&self->original);
	g_signal_emit (self, signals[SIGNAL_EDIT_END], 0, commit);
}

static void
select_key (LrgCurveEdit *self,
            gint          index)
{
	if (self->selected == index)
		return;
	self->selected = index;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTED]);
}

/* move_selected: put the selected key at (@t, @v), between neighbours */
static void
move_selected (LrgCurveEdit *self,
               gfloat        t,
               gfloat        v)
{
	g_autoptr(GArray) keys = read_keys (self->curve);
	Key              *k;
	gfloat            lo = -G_MAXFLOAT;
	gfloat            hi = G_MAXFLOAT;
	gfloat            t0;
	gfloat            t1;

	if (self->selected < 0 || (guint) self->selected >= keys->len)
		return;

	/* Time stays inside the view and strictly between the neighbours */
	time_range (self, &t0, &t1);
	if (self->selected > 0)
		lo = g_array_index (keys, Key, self->selected - 1).t + MIN_GAP;
	if ((guint) self->selected + 1 < keys->len)
		hi = g_array_index (keys, Key, self->selected + 1).t - MIN_GAP;
	k = &g_array_index (keys, Key, self->selected);
	k->t = CLAMP (CLAMP (t, t0, t1), lo, MAX (lo, hi));
	k->value = v;
	write_keys (self->curve, keys);
}

/* one_shot_add: a key on the curve at time @t */
static void
one_shot_add (LrgCurveEdit *self,
              gfloat        t,
              gfloat        v)
{
	g_autoptr(GArray) keys = NULL;
	guint             i;

	begin_gesture (self);
	lrg_keyframe_curve_add_key (self->curve, t, v, LRG_EASING_LINEAR);
	keys = read_keys (self->curve);
	for (i = 0; i < keys->len; i++)
		if (g_array_index (keys, Key, i).t == t)
			select_key (self, (gint) i);
	changed (self);
	end_gesture (self, TRUE);
}

/* one_shot_remove: drop the selected key, keeping at least one */
static gboolean
one_shot_remove (LrgCurveEdit *self)
{
	g_autoptr(GArray) keys = read_keys (self->curve);

	if (self->selected < 0 || (guint) self->selected >= keys->len || keys->len <= 1)
		return FALSE;
	begin_gesture (self);
	g_array_remove_index (keys, (guint) self->selected);
	write_keys (self->curve, keys);
	select_key (self, -1);
	changed (self);
	end_gesture (self, TRUE);
	return TRUE;
}

/* one_shot_cycle_easing: the selected key's easing, +/- one */
static gboolean
one_shot_cycle_easing (LrgCurveEdit *self,
                       gint          delta)
{
	g_autoptr(GArray)     keys = read_keys (self->curve);
	g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_EASING_TYPE);
	Key                  *k;
	gint                  n = (gint) klass->n_values;

	if (self->selected < 0 || (guint) self->selected >= keys->len || n <= 0)
		return FALSE;
	begin_gesture (self);
	k = &g_array_index (keys, Key, self->selected);
	k->easing = (LrgEasingType) ((((gint) k->easing + delta) % n + n) % n);
	write_keys (self->curve, keys);
	changed (self);
	end_gesture (self, TRUE);
	return TRUE;
}

/* ==========================================================================
 * Popup (compact mode)
 * ========================================================================== */

/* on_popup_curve_changed: mirror the popup's curve into ours */
static void
on_popup_curve_changed (LrgCurveEdit *popup,
                        gpointer      user_data)
{
	LrgCurveEdit *self = LRG_CURVE_EDIT (user_data);

	begin_gesture (self);
	g_object_unref (self->curve);
	self->curve = lrg_keyframe_curve_copy (popup->curve);
	changed (self);
}

static void
close_popup_session (LrgCurveEdit *self)
{
	if (self->popup_canvas != NULL && self->dismiss_id != 0)
		g_signal_handler_disconnect (self->popup_canvas, self->dismiss_id);
	self->dismiss_id = 0;
	g_clear_weak_pointer (&self->popup_canvas);
	if (self->popup != NULL)
		g_signal_handlers_disconnect_by_data (self->popup, self);
	g_clear_object (&self->popup);
	lrg_widget_unset_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_OPEN);
	end_gesture (self, TRUE);
}

static void
on_popup_dismissed (LrgCanvas *canvas,
                    LrgWidget *popup,
                    gpointer   user_data)
{
	LrgCurveEdit *self = LRG_CURVE_EDIT (user_data);

	if (self->popup != NULL && popup == LRG_WIDGET (self->popup))
		close_popup_session (self);
}

void
lrg_curve_edit_open_popup (LrgCurveEdit *self)
{
	LrgWidget *root;

	g_return_if_fail (LRG_IS_CURVE_EDIT (self));

	root = lrg_widget_get_root (LRG_WIDGET (self));
	if (!self->compact || self->popup != NULL || !LRG_IS_CANVAS (root))
		return;

	self->popup = lrg_curve_edit_new ();
	self->popup->compact = FALSE;
	self->popup->range_min = self->range_min;
	self->popup->range_max = self->range_max;
	lrg_curve_edit_set_curve (self->popup, self->curve);
	lrg_widget_set_size (LRG_WIDGET (self->popup), FULL_WIDTH, FULL_HEIGHT);
	g_signal_connect (self->popup, "curve-changed", G_CALLBACK (on_popup_curve_changed), self);
	g_set_weak_pointer (&self->popup_canvas, LRG_CANVAS (root));
	self->dismiss_id = g_signal_connect (root, "popup-dismissed", G_CALLBACK (on_popup_dismissed), self);
	lrg_canvas_show_popup (LRG_CANVAS (root), LRG_WIDGET (self->popup), LRG_WIDGET (self),
	                       LRG_POPUP_PLACEMENT_BELOW, FALSE);
}

LrgCurveEdit *
lrg_curve_edit_get_popup_editor (LrgCurveEdit *self)
{
	g_return_val_if_fail (LRG_IS_CURVE_EDIT (self), NULL);
	return self->popup;
}

/* ==========================================================================
 * Layout and painting
 * ========================================================================== */

static void
lrg_curve_edit_measure (LrgWidget *widget,
                        gfloat    *preferred_width,
                        gfloat    *preferred_height)
{
	LrgCurveEdit *self = LRG_CURVE_EDIT (widget);

	if (self->compact)
	{
		*preferred_width = 120.0f;
		*preferred_height = lrg_theme_get_metric (lrg_widget_get_theme (widget),
		                                          LRG_THEME_METRIC_CONTROL_HEIGHT);
	}
	else
	{
		*preferred_width = FULL_WIDTH;
		*preferred_height = FULL_HEIGHT;
	}
}

static void
lrg_curve_edit_style_defaults (LrgWidget      *widget,
                               LrgUIPart       part,
                               LrgWidgetState  state,
                               LrgPartLook    *look)
{
	LrgCurveEdit *self = LRG_CURVE_EDIT (widget);
	LrgTheme     *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_BOX:
		/* Compact: an input field; full: a popup card */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme,
		                              self->compact ? LRG_THEME_COLOR_INPUT : LRG_THEME_COLOR_POPUP));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme,
		                          (self->compact && (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_OPEN)))
		                          ? LRG_THEME_COLOR_ACCENT : LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, self->compact
		                          ? LRG_THEME_METRIC_CORNER_RADIUS : LRG_THEME_METRIC_POPUP_RADIUS));
		if (!self->compact)
		{
			lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
			                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR),
			                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y));
		}
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_FILL:
		/* The curve line */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_size (look, self->compact ? 1.5f : 2.0f);
		return;

	case LRG_UI_PART_TRACK:
		/* Grid lines */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TRACK));
		return;

	case LRG_UI_PART_HANDLE:
		/* Key dots */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_HANDLE));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT), 2.0f);
		return;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL));
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_curve_edit_parent_class)->style_defaults (widget, part, state, look);
}

static void
lrg_curve_edit_paint (LrgWidget    *widget,
                      LrgPaintList *list)
{
	LrgCurveEdit *self = LRG_CURVE_EDIT (widget);
	LrgTheme     *theme = lrg_widget_get_theme (widget);
	LrgPartLook   box;
	LrgPartLook   line;
	LrgPartLook   grid;
	LrgPartLook   dot;
	GrlRectangle  g;
	gfloat        t0;
	gfloat        t1;
	gfloat        px = 0.0f;
	gfloat        py = 0.0f;
	gboolean      faded;
	guint         n;
	guint         i;

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	faded = box.opacity < 1.0f;
	if (faded)
		lrg_paint_list_push_opacity (list, box.opacity);
	box.opacity = 1.0f;
	lrg_widget_paint_box (widget, list, &box, lrg_widget_get_world_x (widget), lrg_widget_get_world_y (widget),
	                      lrg_widget_get_width (widget), lrg_widget_get_height (widget));

	lrg_curve_edit_get_graph_rect (self, &g);
	time_range (self, &t0, &t1);
	lrg_widget_resolve_look (widget, LRG_UI_PART_FILL, &line);
	lrg_widget_resolve_look (widget, LRG_UI_PART_TRACK, &grid);

	/* 1. Grid: quarters in the full editor, a midline in the preview */
	if (!self->compact)
	{
		for (i = 0; i <= 4; i++)
		{
			gfloat fx = g.x + g.width * (gfloat) i / 4.0f;
			gfloat fy = g.y + g.height * (gfloat) i / 4.0f;

			lrg_paint_list_line (list, fx, g.y, fx, g.y + g.height, 1.0f, &grid.foreground);
			lrg_paint_list_line (list, g.x, fy, g.x + g.width, fy, 1.0f, &grid.foreground);
		}
	}
	else
		lrg_paint_list_line (list, g.x, g.y + g.height * 0.5f, g.x + g.width, g.y + g.height * 0.5f, 1.0f,
		                     &grid.foreground);

	/* 2. The curve, sampled */
	lrg_paint_list_push_clip (list, g.x - 2.0f, g.y - 2.0f, g.width + 4.0f, g.height + 4.0f);
	n = lrg_keyframe_curve_get_key_count (self->curve);
	if (n > 0)
	{
		for (i = 0; i <= SAMPLES; i++)
		{
			gfloat t = t0 + (t1 - t0) * (gfloat) i / (gfloat) SAMPLES;
			gfloat x;
			gfloat y;

			to_screen (self, t, lrg_keyframe_curve_sample (self->curve, t), &x, &y);
			if (i > 0)
				lrg_paint_list_line (list, px, py, x, y, line.size > 0.0f ? line.size : 2.0f,
				                     &line.foreground);
			px = x;
			py = y;
		}
	}
	lrg_paint_list_pop_clip (list);

	/* 3. Keys (full editor only), the selected one filled with accent */
	if (!self->compact)
	{
		LrgPartLook label;
		GrlFont    *font = lrg_theme_get_ui_font (theme);

		lrg_widget_resolve_look (widget, LRG_UI_PART_HANDLE, &dot);
		for (i = 0; i < n; i++)
		{
			gfloat kx;
			gfloat ky;
			gboolean sel = (gint) i == self->selected;

			lrg_curve_edit_get_key_position (self, i, &kx, &ky);
			lrg_paint_list_circle (list, kx, ky, KEY_RADIUS + 1.5f, &dot.border);
			lrg_paint_list_circle (list, kx, ky, KEY_RADIUS, sel ? &dot.border : &dot.background);
		}

		/* 4. Footer: the selected key's time, value and easing */
		lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
		if (self->selected >= 0)
		{
			g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_EASING_TYPE);
			g_autofree gchar     *text = NULL;
			GEnumValue           *ev;
			gfloat                t;
			gfloat                v;
			LrgEasingType         easing;

			lrg_keyframe_curve_get_key (self->curve, (guint) self->selected, &t, &v, &easing);
			ev = g_enum_get_value (klass, (gint) easing);
			text = g_strdup_printf ("t %.3f   value %.3f   %s", (gdouble) t, (gdouble) v,
			                        ev != NULL ? ev->value_nick : "?");
			lrg_paint_list_text (list, font, text, g.x, g.y + g.height + 4.0f,
			                     label.size > 0.0f ? label.size : 12.0f, 1.0f, &label.foreground);
		}
		else
			lrg_paint_list_text (list, font, "Click to add a key, drag to move", g.x, g.y + g.height + 4.0f,
			                     label.size > 0.0f ? label.size : 12.0f, 1.0f, &label.foreground);
	}

	if (faded)
		lrg_paint_list_pop_opacity (list);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

static gboolean
handle_full_event (LrgCurveEdit     *self,
                   const LrgUIEvent *event)
{
	gfloat px = lrg_ui_event_get_x (event);
	gfloat py = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			GrlRectangle g;
			gint         hit;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			hit = key_at (self, px, py);
			if (hit >= 0)
			{
				select_key (self, hit);
				self->pressing = TRUE;
				self->dragging = FALSE;
				self->press_x = px;
				self->press_y = py;
				return TRUE;
			}

			/* Empty graph: add a key on the curve at that time */
			lrg_curve_edit_get_graph_rect (self, &g);
			if (px >= g.x && px <= g.x + g.width && py >= g.y && py <= g.y + g.height)
			{
				gfloat t;
				gfloat v;

				from_screen (self, px, py, &t, &v);
				one_shot_add (self, t, v);
				return TRUE;
			}
			return FALSE;
		}

	case LRG_UI_EVENT_MOUSE_MOVE:
		if (self->pressing && self->selected >= 0)
		{
			gfloat t;
			gfloat v;

			if (!self->dragging && fabsf (px - self->press_x) < DRAG_THRESHOLD &&
			    fabsf (py - self->press_y) < DRAG_THRESHOLD)
				return TRUE;
			if (!self->dragging)
			{
				self->dragging = TRUE;
				begin_gesture (self);
			}
			from_screen (self, px, py, &t, &v);
			move_selected (self, t, v);
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
			self->dragging = FALSE;
			end_gesture (self, TRUE);
		}
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		if (self->pressing && self->dragging)
		{
			self->pressing = FALSE;
			self->dragging = FALSE;
			end_gesture (self, FALSE);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat dy = lrg_ui_event_get_scroll_y (event);

			if (dy == 0.0f || self->selected < 0)
				return FALSE;
			return one_shot_cycle_easing (self, dy > 0.0f ? 1 : -1);
		}

	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_DELETE:
		case GRL_KEY_BACKSPACE:
			return one_shot_remove (self);
		case GRL_KEY_E:
			return one_shot_cycle_easing (self, (lrg_ui_event_get_modifiers (event) & LRG_INPUT_MODIFIER_SHIFT)
			                                    ? -1 : 1);
		default:
			return FALSE;
		}

	case LRG_UI_EVENT_FOCUS_OUT:
		if (self->pressing && self->dragging)
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

static gboolean
lrg_curve_edit_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
	LrgCurveEdit *self = LRG_CURVE_EDIT (widget);

	if (!self->compact)
		return handle_full_event (self, event);

	/* Compact: a click or ACTIVATE opens the popup editor */
	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return lrg_ui_event_get_button (event) == 0;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (lrg_widget_contains_point (widget, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event)))
			lrg_curve_edit_open_popup (self);
		return TRUE;

	case LRG_UI_EVENT_ACTIVATE:
		lrg_curve_edit_open_popup (self);
		return TRUE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_curve_edit_dispose (GObject *object)
{
	LrgCurveEdit *self = LRG_CURVE_EDIT (object);

	if (self->popup != NULL)
	{
		LrgCanvas *canvas = self->popup_canvas;
		LrgWidget *popup = LRG_WIDGET (g_object_ref (self->popup));

		close_popup_session (self);
		if (canvas != NULL)
			lrg_canvas_dismiss_popup (canvas, popup);
		g_object_unref (popup);
	}
	G_OBJECT_CLASS (lrg_curve_edit_parent_class)->dispose (object);
}

static void
lrg_curve_edit_finalize (GObject *object)
{
	LrgCurveEdit *self = LRG_CURVE_EDIT (object);

	g_clear_object (&self->curve);
	g_clear_object (&self->original);
	G_OBJECT_CLASS (lrg_curve_edit_parent_class)->finalize (object);
}

static void
lrg_curve_edit_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgCurveEdit *self = LRG_CURVE_EDIT (object);

	switch (prop_id)
	{
	case PROP_CURVE:
		g_value_set_object (value, self->curve);
		break;
	case PROP_COMPACT:
		g_value_set_boolean (value, self->compact);
		break;
	case PROP_SELECTED:
		g_value_set_int (value, self->selected);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_curve_edit_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgCurveEdit *self = LRG_CURVE_EDIT (object);

	switch (prop_id)
	{
	case PROP_CURVE:
		lrg_curve_edit_set_curve (self, g_value_get_object (value));
		break;
	case PROP_COMPACT:
		lrg_curve_edit_set_compact (self, g_value_get_boolean (value));
		break;
	case PROP_SELECTED:
		lrg_curve_edit_set_selected (self, g_value_get_int (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_curve_edit_class_init (LrgCurveEditClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_curve_edit_dispose;
	object_class->finalize = lrg_curve_edit_finalize;
	object_class->get_property = lrg_curve_edit_get_property;
	object_class->set_property = lrg_curve_edit_set_property;

	widget_class->measure = lrg_curve_edit_measure;
	widget_class->paint = lrg_curve_edit_paint;
	widget_class->style_defaults = lrg_curve_edit_style_defaults;
	widget_class->handle_event = lrg_curve_edit_handle_event;

	/**
	 * LrgCurveEdit:curve:
	 *
	 * The edited #LrgKeyframeCurve (setting it copies the curve).
	 */
	properties[PROP_CURVE] =
		g_param_spec_object ("curve", NULL, NULL, LRG_TYPE_KEYFRAME_CURVE,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgCurveEdit:compact:
	 *
	 * %TRUE for a preview that opens a popup editor, %FALSE to edit in
	 * place.
	 */
	properties[PROP_COMPACT] =
		g_param_spec_boolean ("compact", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgCurveEdit:selected:
	 *
	 * The selected key index, -1 for none.
	 */
	properties[PROP_SELECTED] =
		g_param_spec_int ("selected", NULL, NULL, -1, G_MAXINT, -1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgCurveEdit::edit-begin:
	 * @self: the editor
	 *
	 * A gesture starts: a key drag moved, a click adds or removes a
	 * key, an easing changes, or the popup editor made its first change.
	 */
	signals[SIGNAL_EDIT_BEGIN] =
		g_signal_new ("edit-begin", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgCurveEdit::curve-changed:
	 * @self: the editor
	 *
	 * The keys changed, always between #LrgCurveEdit::edit-begin and
	 * #LrgCurveEdit::edit-end.
	 */
	signals[SIGNAL_CURVE_CHANGED] =
		g_signal_new ("curve-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgCurveEdit::edit-end:
	 * @self: the editor
	 * @commit: %FALSE when the gesture was cancelled and the keys were
	 *   put back
	 *
	 * The gesture ended.
	 */
	signals[SIGNAL_EDIT_END] =
		g_signal_new ("edit-end", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
lrg_curve_edit_init (LrgCurveEdit *self)
{
	self->curve = default_curve ();
	self->compact = TRUE;
	self->selected = -1;
	self->range_min = 0.0f;
	self->range_max = 1.0f;
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgCurveEdit *
lrg_curve_edit_new (void)
{
	return g_object_new (LRG_TYPE_CURVE_EDIT, NULL);
}

LrgKeyframeCurve *
lrg_curve_edit_get_curve (LrgCurveEdit *self)
{
	g_return_val_if_fail (LRG_IS_CURVE_EDIT (self), NULL);
	return self->curve;
}

void
lrg_curve_edit_set_curve (LrgCurveEdit     *self,
                          LrgKeyframeCurve *curve)
{
	g_return_if_fail (LRG_IS_CURVE_EDIT (self));
	g_return_if_fail (curve == NULL || LRG_IS_KEYFRAME_CURVE (curve));

	if (curve != NULL && curves_equal (curve, self->curve))
		return;
	g_object_unref (self->curve);
	self->curve = curve != NULL ? lrg_keyframe_curve_copy (curve) : default_curve ();
	if (self->selected >= (gint) lrg_keyframe_curve_get_key_count (self->curve))
		select_key (self, -1);
	if (self->popup != NULL)
		lrg_curve_edit_set_curve (self->popup, self->curve);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURVE]);
}

gboolean
lrg_curve_edit_get_compact (LrgCurveEdit *self)
{
	g_return_val_if_fail (LRG_IS_CURVE_EDIT (self), TRUE);
	return self->compact;
}

void
lrg_curve_edit_set_compact (LrgCurveEdit *self,
                            gboolean      compact)
{
	g_return_if_fail (LRG_IS_CURVE_EDIT (self));

	compact = !!compact;
	if (self->compact == compact)
		return;
	self->compact = compact;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COMPACT]);
}

gint
lrg_curve_edit_get_selected (LrgCurveEdit *self)
{
	g_return_val_if_fail (LRG_IS_CURVE_EDIT (self), -1);
	return self->selected;
}

void
lrg_curve_edit_set_selected (LrgCurveEdit *self,
                             gint          index)
{
	g_return_if_fail (LRG_IS_CURVE_EDIT (self));

	if (index < -1 || index >= (gint) lrg_keyframe_curve_get_key_count (self->curve))
		index = -1;
	select_key (self, index);
}

void
lrg_curve_edit_set_value_range (LrgCurveEdit *self,
                                gfloat        min,
                                gfloat        max)
{
	g_return_if_fail (LRG_IS_CURVE_EDIT (self));
	g_return_if_fail (isfinite (min) && isfinite (max));

	self->range_min = MIN (min, max);
	self->range_max = MAX (min, max);
	if (self->popup != NULL)
		lrg_curve_edit_set_value_range (self->popup, min, max);
}
