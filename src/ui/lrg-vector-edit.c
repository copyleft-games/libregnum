/* lrg-vector-edit.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVectorEdit: per-axis scrub fields for 1 to 4 component vectors.
 *
 * One widget paints every field itself (no child widgets), so the
 * whole vector is one focus stop and one pointer capture: a scrub can
 * leave the field sideways without losing the gesture.
 *
 * Each field: a small rounded tab in the axis colour carrying the axis
 * letter, then the formatted value on an INPUT-coloured box (BOX part).
 * The optional lock button at the end toggles the uniform lock.
 *
 * Gestures: a press on a field arms it; moving past a small threshold
 * starts a scrub (edit-begin, then value-changed every frame, edit-end
 * on release). CANCEL (Escape) mid-scrub restores the values and ends
 * the gesture with commit FALSE. A press and release without moving
 * starts typing into that field: TEXT_INPUT characters build a number,
 * Enter (or losing focus) applies it as a one-shot gesture, Escape
 * abandons it.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-vector-edit.h"
#include "../paint/lrg-paint-icons.h"
#include "../lrg-log.h"

/* Units of horizontal drag per step */
#define SCRUB_UNITS    (2.0f)

/* Pointer travel before a press becomes a scrub */
#define DRAG_THRESHOLD (3.0f)

/* Gap between fields */
#define FIELD_GAP      (4.0f)

/* Axis colours: X red, Y green, Z blue, W amber */
static const GrlColor axis_colors[LRG_VECTOR_EDIT_MAX_COMPONENTS] = {
	{ 0xe0, 0x4f, 0x5f, 0xff },
	{ 0x5c, 0xb8, 0x5c, 0xff },
	{ 0x4a, 0x8c, 0xe8, 0xff },
	{ 0xe0, 0xa8, 0x3c, 0xff }
};

static const gchar *axis_letters[LRG_VECTOR_EDIT_MAX_COMPONENTS] = { "X", "Y", "Z", "W" };

struct _LrgVectorEdit
{
	LrgWidget  parent_instance;

	guint      n;
	gfloat     values[LRG_VECTOR_EDIT_MAX_COMPONENTS];
	gboolean   mixed[LRG_VECTOR_EDIT_MAX_COMPONENTS];
	gdouble    min;
	gdouble    max;
	gdouble    step;
	guint      digits;
	gboolean   uniform;
	gboolean   show_uniform;

	/* Pointer gesture */
	gint       press_axis;     /* field pressed, -1 none */
	gboolean   press_lock;     /* the lock button was pressed */
	gboolean   scrubbing;
	gboolean   in_gesture;
	gfloat     press_x;
	gfloat     press_y;
	gfloat     original[LRG_VECTOR_EDIT_MAX_COMPONENTS];
	gboolean   original_mixed[LRG_VECTOR_EDIT_MAX_COMPONENTS];
	gint       hover_axis;

	/* Typing */
	gint       edit_axis;      /* -1 when not typing */
	GString   *edit_text;
	gboolean   replace_on_type;
};

G_DEFINE_FINAL_TYPE (LrgVectorEdit, lrg_vector_edit, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_N_COMPONENTS,
	PROP_STEP,
	PROP_DIGITS,
	PROP_UNIFORM,
	PROP_SHOW_UNIFORM,
	N_PROPS
};

enum
{
	SIGNAL_EDIT_BEGIN,
	SIGNAL_VALUE_CHANGED,
	SIGNAL_EDIT_END,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* ==========================================================================
 * Geometry
 * ========================================================================== */

/* lock_width: the square lock button, or 0 when hidden */
static gfloat
lock_width (LrgVectorEdit *self)
{
	return self->show_uniform ? lrg_widget_get_height (LRG_WIDGET (self)) : 0.0f;
}

gboolean
lrg_vector_edit_get_field_rect (LrgVectorEdit *self,
                                guint          axis,
                                GrlRectangle  *rect)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     lw;
	gfloat     total;
	gfloat     fw;

	g_return_val_if_fail (LRG_IS_VECTOR_EDIT (self), FALSE);
	g_return_val_if_fail (rect != NULL, FALSE);

	if (axis >= self->n)
		return FALSE;

	/* Fields share the width left of the lock button equally */
	lw = lock_width (self);
	total = lrg_widget_get_width (w) - lw - (lw > 0.0f ? FIELD_GAP : 0.0f);
	fw = MAX (1.0f, (total - FIELD_GAP * (gfloat) (self->n - 1)) / (gfloat) self->n);
	rect->x = lrg_widget_get_world_x (w) + (fw + FIELD_GAP) * (gfloat) axis;
	rect->y = lrg_widget_get_world_y (w);
	rect->width = fw;
	rect->height = lrg_widget_get_height (w);
	return TRUE;
}

gboolean
lrg_vector_edit_get_lock_rect (LrgVectorEdit *self,
                               GrlRectangle  *rect)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     lw;

	g_return_val_if_fail (LRG_IS_VECTOR_EDIT (self), FALSE);
	g_return_val_if_fail (rect != NULL, FALSE);

	lw = lock_width (self);
	if (lw <= 0.0f)
		return FALSE;
	rect->x = lrg_widget_get_world_x (w) + lrg_widget_get_width (w) - lw;
	rect->y = lrg_widget_get_world_y (w);
	rect->width = lw;
	rect->height = lrg_widget_get_height (w);
	return TRUE;
}

/* axis_at: the field under (@px, @py), or -1 */
static gint
axis_at (LrgVectorEdit *self,
         gfloat         px,
         gfloat         py)
{
	GrlRectangle r;
	guint        i;

	for (i = 0; i < self->n; i++)
		if (lrg_vector_edit_get_field_rect (self, i, &r) &&
		    px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height)
			return (gint) i;
	return -1;
}

/* in_lock: whether (@px, @py) is on the lock button */
static gboolean
in_lock (LrgVectorEdit *self,
         gfloat         px,
         gfloat         py)
{
	GrlRectangle r;

	return lrg_vector_edit_get_lock_rect (self, &r) &&
	       px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height;
}

/* ==========================================================================
 * Values and gestures
 * ========================================================================== */

/* clamp_value: into the range, tidy floating dust */
static gfloat
clamp_value (LrgVectorEdit *self,
             gdouble        v)
{
	v = CLAMP (v, self->min, self->max);
	return (gfloat) (round (v * 1e6) / 1e6);
}

static void
begin_gesture (LrgVectorEdit *self)
{
	if (self->in_gesture)
		return;
	self->in_gesture = TRUE;
	memcpy (self->original, self->values, sizeof (self->values));
	memcpy (self->original_mixed, self->mixed, sizeof (self->mixed));
	g_signal_emit (self, signals[SIGNAL_EDIT_BEGIN], 0);
}

static void
changed (LrgVectorEdit *self)
{
	g_signal_emit (self, signals[SIGNAL_VALUE_CHANGED], 0);
}

static void
end_gesture (LrgVectorEdit *self,
             gboolean       commit)
{
	if (!self->in_gesture)
		return;
	self->in_gesture = FALSE;
	if (!commit)
	{
		memcpy (self->values, self->original, sizeof (self->values));
		memcpy (self->mixed, self->original_mixed, sizeof (self->mixed));
		changed (self);
	}
	g_signal_emit (self, signals[SIGNAL_EDIT_END], 0, commit);
}

/*
 * apply_axis:
 *
 * Gives @axis the value @v (from the gesture's starting values). With
 * the uniform lock, the other axes scale by the same ratio, or shift
 * by the same amount when the axis started at zero.
 */
static void
apply_axis (LrgVectorEdit *self,
            guint          axis,
            gdouble        v)
{
	guint i;

	v = clamp_value (self, v);
	if (self->uniform && self->n > 1)
	{
		gdouble from = self->original[axis];

		for (i = 0; i < self->n; i++)
		{
			if (i == axis)
				continue;
			if (fabs (from) > 1e-9)
				self->values[i] = clamp_value (self, self->original[i] * (v / from));
			else
				self->values[i] = clamp_value (self, self->original[i] + (v - from));
			self->mixed[i] = FALSE;
		}
	}
	self->values[axis] = (gfloat) v;
	self->mixed[axis] = FALSE;
}

/* ==========================================================================
 * Typing
 * ========================================================================== */

static gchar *
format_axis (LrgVectorEdit *self,
             guint          axis)
{
	return g_strdup_printf ("%.*f", (gint) self->digits, (gdouble) self->values[axis]);
}

static void
start_typing (LrgVectorEdit *self,
              guint          axis)
{
	g_autofree gchar *text = format_axis (self, axis);

	self->edit_axis = (gint) axis;
	g_string_assign (self->edit_text, self->mixed[axis] ? "" : text);
	self->replace_on_type = TRUE;
}

/* finish_typing: apply the typed number (one-shot), or drop it */
static void
finish_typing (LrgVectorEdit *self,
               gboolean       apply)
{
	gint    axis = self->edit_axis;
	gchar  *end = NULL;
	gdouble v;

	if (axis < 0)
		return;
	self->edit_axis = -1;
	if (!apply || self->edit_text->len == 0)
		return;

	v = g_ascii_strtod (self->edit_text->str, &end);
	if (end == self->edit_text->str || *end != '\0' || !isfinite (v))
		return;
	if (!self->mixed[axis] && clamp_value (self, v) == self->values[axis])
		return;

	begin_gesture (self);
	apply_axis (self, (guint) axis, v);
	changed (self);
	end_gesture (self, TRUE);
}

/* ==========================================================================
 * Layout and painting
 * ========================================================================== */

static void
lrg_vector_edit_measure (LrgWidget *widget,
                         gfloat    *preferred_width,
                         gfloat    *preferred_height)
{
	LrgVectorEdit *self = LRG_VECTOR_EDIT (widget);
	gfloat         ch = lrg_theme_get_metric (lrg_widget_get_theme (widget), LRG_THEME_METRIC_CONTROL_HEIGHT);

	*preferred_width = 70.0f * (gfloat) self->n + FIELD_GAP * (gfloat) (self->n - 1) +
	                   (self->show_uniform ? ch + FIELD_GAP : 0.0f);
	*preferred_height = ch;
}

static void
lrg_vector_edit_style_defaults (LrgWidget      *widget,
                                LrgUIPart       part,
                                LrgWidgetState  state,
                                LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_BOX:
		/* Each field: an input box */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE));
		return;

	case LRG_UI_PART_KNOB:
		/* The lock button */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_CHECKED) ? LRG_THEME_COLOR_ACCENT
		                                                                  : LRG_THEME_COLOR_CONTROL));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_CHECKED) ? LRG_THEME_COLOR_TEXT_ON_ACCENT
		                                                                  : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_vector_edit_parent_class)->style_defaults (widget, part, state, look);
}

static void
lrg_vector_edit_paint (LrgWidget    *widget,
                       LrgPaintList *list)
{
	LrgVectorEdit *self = LRG_VECTOR_EDIT (widget);
	LrgTheme      *theme = lrg_widget_get_theme (widget);
	GrlFont       *font = lrg_theme_get_ui_font (theme);
	const GrlColor *accent = lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT);
	GrlColor       white = { 255, 255, 255, 255 };
	LrgPartLook    box;
	LrgPartLook    label;
	GrlRectangle   r;
	gboolean       faded;
	guint          i;

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
	faded = box.opacity < 1.0f;
	if (faded)
		lrg_paint_list_push_opacity (list, box.opacity);
	box.opacity = 1.0f;

	for (i = 0; i < self->n; i++)
	{
		LrgPartLook       field = box;
		LrgCornerRadii    tab_radii;
		g_autofree gchar *text = NULL;
		gfloat            tab_w;
		gfloat            tw = 0.0f;
		gfloat            th = 0.0f;
		gfloat            size = label.size > 0.0f ? label.size : 14.0f;
		gboolean          active = (gint) i == self->edit_axis || (self->scrubbing && (gint) i == self->press_axis);

		lrg_vector_edit_get_field_rect (self, i, &r);
		tab_w = MIN (r.width * 0.4f, roundf (size * 1.2f));

		/* 1. Field box; the active or hovered one outlined in accent */
		if (active || (gint) i == self->hover_axis)
			field.border = *accent;
		lrg_widget_paint_box (widget, list, &field, r.x, r.y, r.width, r.height);

		/* 2. Axis tab, rounded on the left only */
		tab_radii.top_left = box.radii.top_left;
		tab_radii.bottom_left = box.radii.bottom_left;
		tab_radii.top_right = 0.0f;
		tab_radii.bottom_right = 0.0f;
		lrg_paint_list_rect_ex (list, r.x, r.y, tab_w, r.height, &tab_radii, &axis_colors[i], NULL, 0.0f, NULL);
		lrg_paint_measure_text (font, axis_letters[i], size * 0.85f, 1.0f, &tw, &th);
		lrg_paint_list_text (list, font, axis_letters[i], roundf (r.x + (tab_w - tw) * 0.5f),
		                     roundf (r.y + (r.height - th) * 0.5f), size * 0.85f, 1.0f, &white);

		/* 3. Value, typed text with a caret, or an em dash when mixed */
		if ((gint) i == self->edit_axis)
			text = g_strdup_printf ("%s|", self->edit_text->str);
		else if (self->mixed[i])
			text = g_strdup ("\xe2\x80\x94");
		else
			text = format_axis (self, i);
		lrg_paint_measure_text (font, text, size, 1.0f, &tw, &th);
		lrg_paint_list_push_clip (list, r.x + tab_w, r.y, MAX (0.0f, r.width - tab_w), r.height);
		lrg_paint_list_text (list, font, text,
		                     roundf (r.x + tab_w + MAX (4.0f, (r.width - tab_w - tw) * 0.5f)),
		                     roundf (r.y + (r.height - th) * 0.5f), size, 1.0f,
		                     active ? accent : &label.foreground);
		lrg_paint_list_pop_clip (list);
	}

	/* 4. Lock button */
	if (lrg_vector_edit_get_lock_rect (self, &r))
	{
		LrgPartLook knob;

		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_KNOB,
		                                   self->uniform ? LRG_WIDGET_STATE_CHECKED : LRG_WIDGET_STATE_NONE,
		                                   &knob);
		lrg_widget_paint_box (widget, list, &knob, r.x, r.y, r.width, r.height);
		lrg_paint_list_icon (list, LRG_ICON_LOCK, r.x + r.width * 0.2f, r.y + r.height * 0.2f,
		                     MIN (r.width, r.height) * 0.6f, 0.0f, &knob.foreground);
	}

	if (faded)
		lrg_paint_list_pop_opacity (list);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* scrub_speed: Shift slows down, Ctrl speeds up */
static gdouble
scrub_speed (const LrgUIEvent *event)
{
	LrgInputModifiers mods = lrg_ui_event_get_modifiers (event);

	if (mods & LRG_INPUT_MODIFIER_SHIFT)
		return 0.1;
	if (mods & LRG_INPUT_MODIFIER_CTRL)
		return 10.0;
	return 1.0;
}

/* typing_char: whether @c may appear in a typed number */
static gboolean
typing_char (gunichar c)
{
	return g_ascii_isdigit ((gchar) c) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E';
}

static gboolean
lrg_vector_edit_handle_event (LrgWidget        *widget,
                              const LrgUIEvent *event)
{
	LrgVectorEdit *self = LRG_VECTOR_EDIT (widget);
	gfloat         px = lrg_ui_event_get_x (event);
	gfloat         py = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;

		/* A press elsewhere applies what was typed */
		finish_typing (self, TRUE);
		self->press_lock = in_lock (self, px, py);
		self->press_axis = self->press_lock ? -1 : axis_at (self, px, py);
		self->scrubbing = FALSE;
		self->press_x = px;
		self->press_y = py;
		return self->press_lock || self->press_axis >= 0;

	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover_axis = axis_at (self, px, py);
		if (self->press_axis >= 0)
		{
			gdouble from;

			if (!self->scrubbing && fabsf (px - self->press_x) < DRAG_THRESHOLD)
				return TRUE;

			/* First real movement: the scrub gesture starts */
			if (!self->scrubbing)
			{
				self->scrubbing = TRUE;
				begin_gesture (self);
				self->press_x = px;
			}
			from = self->original_mixed[self->press_axis] ? 0.0 : (gdouble) self->original[self->press_axis];
			apply_axis (self, (guint) self->press_axis,
			            from + (gdouble) ((px - self->press_x) / SCRUB_UNITS) * self->step * scrub_speed (event));
			changed (self);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_axis = -1;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (self->press_lock)
		{
			self->press_lock = FALSE;
			if (in_lock (self, px, py))
				lrg_vector_edit_set_uniform (self, !self->uniform);
			return TRUE;
		}
		if (self->press_axis < 0)
			return FALSE;
		if (self->scrubbing)
			end_gesture (self, TRUE);
		else
			start_typing (self, (guint) self->press_axis);
		self->scrubbing = FALSE;
		self->press_axis = -1;
		return TRUE;

	case LRG_UI_EVENT_CANCEL:
		/* Escape mid-scrub: put the values back */
		if (self->scrubbing)
		{
			self->scrubbing = FALSE;
			self->press_axis = -1;
			end_gesture (self, FALSE);
			return TRUE;
		}
		if (self->edit_axis >= 0)
		{
			finish_typing (self, FALSE);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_ACTIVATE:
		if (self->edit_axis < 0 && self->n > 0)
		{
			start_typing (self, 0);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_TEXT_INPUT:
		{
			gunichar c = lrg_ui_event_get_codepoint (event);

			if (self->edit_axis < 0 || !typing_char (c))
				return self->edit_axis >= 0;
			if (self->replace_on_type)
				g_string_truncate (self->edit_text, 0);
			self->replace_on_type = FALSE;
			g_string_append_c (self->edit_text, (gchar) c);
			return TRUE;
		}

	case LRG_UI_EVENT_KEY_DOWN:
		if (self->edit_axis < 0)
			return FALSE;
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_BACKSPACE:
			if (self->replace_on_type)
				g_string_truncate (self->edit_text, 0);
			else if (self->edit_text->len > 0)
				g_string_truncate (self->edit_text, self->edit_text->len - 1);
			self->replace_on_type = FALSE;
			return TRUE;
		case GRL_KEY_ENTER:
		case GRL_KEY_KP_ENTER:
			finish_typing (self, TRUE);
			return TRUE;
		case GRL_KEY_ESCAPE:
			finish_typing (self, FALSE);
			return TRUE;
		case GRL_KEY_TAB:
			/* Tab: apply, then move to the next axis while there is one */
			{
				gint next = self->edit_axis + 1;

				finish_typing (self, TRUE);
				if (next < (gint) self->n)
				{
					start_typing (self, (guint) next);
					return TRUE;
				}
				return FALSE;
			}
		default:
			/* Typing swallows the keys of the characters it takes */
			return TRUE;
		}

	case LRG_UI_EVENT_FOCUS_OUT:
		finish_typing (self, TRUE);
		if (self->scrubbing)
		{
			self->scrubbing = FALSE;
			self->press_axis = -1;
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
lrg_vector_edit_finalize (GObject *object)
{
	LrgVectorEdit *self = LRG_VECTOR_EDIT (object);

	g_string_free (self->edit_text, TRUE);
	G_OBJECT_CLASS (lrg_vector_edit_parent_class)->finalize (object);
}

static void
lrg_vector_edit_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	LrgVectorEdit *self = LRG_VECTOR_EDIT (object);

	switch (prop_id)
	{
	case PROP_N_COMPONENTS:
		g_value_set_uint (value, self->n);
		break;
	case PROP_STEP:
		g_value_set_double (value, self->step);
		break;
	case PROP_DIGITS:
		g_value_set_uint (value, self->digits);
		break;
	case PROP_UNIFORM:
		g_value_set_boolean (value, self->uniform);
		break;
	case PROP_SHOW_UNIFORM:
		g_value_set_boolean (value, self->show_uniform);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_vector_edit_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	LrgVectorEdit *self = LRG_VECTOR_EDIT (object);

	switch (prop_id)
	{
	case PROP_N_COMPONENTS:
		lrg_vector_edit_set_n_components (self, g_value_get_uint (value));
		break;
	case PROP_STEP:
		lrg_vector_edit_set_step (self, g_value_get_double (value));
		break;
	case PROP_DIGITS:
		lrg_vector_edit_set_digits (self, g_value_get_uint (value));
		break;
	case PROP_UNIFORM:
		lrg_vector_edit_set_uniform (self, g_value_get_boolean (value));
		break;
	case PROP_SHOW_UNIFORM:
		lrg_vector_edit_set_show_uniform (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_vector_edit_class_init (LrgVectorEditClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->finalize = lrg_vector_edit_finalize;
	object_class->get_property = lrg_vector_edit_get_property;
	object_class->set_property = lrg_vector_edit_set_property;

	widget_class->measure = lrg_vector_edit_measure;
	widget_class->paint = lrg_vector_edit_paint;
	widget_class->style_defaults = lrg_vector_edit_style_defaults;
	widget_class->handle_event = lrg_vector_edit_handle_event;

	/**
	 * LrgVectorEdit:n-components:
	 *
	 * Number of axes shown, 1 to 4.
	 */
	properties[PROP_N_COMPONENTS] =
		g_param_spec_uint ("n-components", NULL, NULL, 1, LRG_VECTOR_EDIT_MAX_COMPONENTS, 3,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgVectorEdit:step:
	 *
	 * Change per two units of horizontal scrubbing.
	 */
	properties[PROP_STEP] =
		g_param_spec_double ("step", NULL, NULL, 0.0, G_MAXDOUBLE, 0.05,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgVectorEdit:digits:
	 *
	 * Decimals shown.
	 */
	properties[PROP_DIGITS] =
		g_param_spec_uint ("digits", NULL, NULL, 0, 9, 3,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgVectorEdit:uniform:
	 *
	 * Whether scrubbing one axis scales the others with it.
	 */
	properties[PROP_UNIFORM] =
		g_param_spec_boolean ("uniform", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgVectorEdit:show-uniform:
	 *
	 * Whether the uniform lock button is shown.
	 */
	properties[PROP_SHOW_UNIFORM] =
		g_param_spec_boolean ("show-uniform", NULL, NULL, FALSE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgVectorEdit::edit-begin:
	 * @self: the editor
	 *
	 * A scrub moved, or a typed value is about to be applied.
	 */
	signals[SIGNAL_EDIT_BEGIN] =
		g_signal_new ("edit-begin", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgVectorEdit::value-changed:
	 * @self: the editor
	 *
	 * Components changed, always between #LrgVectorEdit::edit-begin and
	 * #LrgVectorEdit::edit-end.
	 */
	signals[SIGNAL_VALUE_CHANGED] =
		g_signal_new ("value-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);

	/**
	 * LrgVectorEdit::edit-end:
	 * @self: the editor
	 * @commit: %FALSE when a scrub was cancelled and the values put back
	 *
	 * The gesture ended.
	 */
	signals[SIGNAL_EDIT_END] =
		g_signal_new ("edit-end", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void
lrg_vector_edit_init (LrgVectorEdit *self)
{
	self->n = 3;
	self->min = -G_MAXFLOAT;
	self->max = G_MAXFLOAT;
	self->step = 0.05;
	self->digits = 3;
	self->press_axis = -1;
	self->hover_axis = -1;
	self->edit_axis = -1;
	self->edit_text = g_string_new (NULL);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgVectorEdit *
lrg_vector_edit_new (guint n_components)
{
	return g_object_new (LRG_TYPE_VECTOR_EDIT, "n-components",
	                     CLAMP (n_components, 1u, (guint) LRG_VECTOR_EDIT_MAX_COMPONENTS), NULL);
}

guint
lrg_vector_edit_get_n_components (LrgVectorEdit *self)
{
	g_return_val_if_fail (LRG_IS_VECTOR_EDIT (self), 0);
	return self->n;
}

void
lrg_vector_edit_set_n_components (LrgVectorEdit *self,
                                  guint          n_components)
{
	g_return_if_fail (LRG_IS_VECTOR_EDIT (self));
	g_return_if_fail (n_components >= 1 && n_components <= LRG_VECTOR_EDIT_MAX_COMPONENTS);

	if (self->n == n_components)
		return;
	finish_typing (self, FALSE);
	self->n = n_components;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_COMPONENTS]);
}

gfloat
lrg_vector_edit_get_component (LrgVectorEdit *self,
                               guint          axis)
{
	g_return_val_if_fail (LRG_IS_VECTOR_EDIT (self), 0.0f);
	g_return_val_if_fail (axis < LRG_VECTOR_EDIT_MAX_COMPONENTS, 0.0f);
	return self->values[axis];
}

void
lrg_vector_edit_set_component (LrgVectorEdit *self,
                               guint          axis,
                               gfloat         value)
{
	g_return_if_fail (LRG_IS_VECTOR_EDIT (self));
	g_return_if_fail (axis < LRG_VECTOR_EDIT_MAX_COMPONENTS);

	self->values[axis] = clamp_value (self, (gdouble) value);
}

void
lrg_vector_edit_get_values (LrgVectorEdit *self,
                            gfloat        *values)
{
	g_return_if_fail (LRG_IS_VECTOR_EDIT (self));
	g_return_if_fail (values != NULL);
	memcpy (values, self->values, sizeof (self->values));
}

void
lrg_vector_edit_set_values (LrgVectorEdit *self,
                            const gfloat  *values,
                            guint          n_values)
{
	guint i;

	g_return_if_fail (LRG_IS_VECTOR_EDIT (self));
	g_return_if_fail (values != NULL || n_values == 0);

	for (i = 0; i < MIN (n_values, (guint) LRG_VECTOR_EDIT_MAX_COMPONENTS); i++)
		self->values[i] = clamp_value (self, (gdouble) values[i]);
}

void
lrg_vector_edit_set_range (LrgVectorEdit *self,
                           gdouble        min,
                           gdouble        max)
{
	guint i;

	g_return_if_fail (LRG_IS_VECTOR_EDIT (self));
	g_return_if_fail (min <= max);

	self->min = min;
	self->max = max;
	for (i = 0; i < LRG_VECTOR_EDIT_MAX_COMPONENTS; i++)
		self->values[i] = clamp_value (self, (gdouble) self->values[i]);
}

gdouble
lrg_vector_edit_get_step (LrgVectorEdit *self)
{
	g_return_val_if_fail (LRG_IS_VECTOR_EDIT (self), 0.0);
	return self->step;
}

void
lrg_vector_edit_set_step (LrgVectorEdit *self,
                          gdouble        step)
{
	g_return_if_fail (LRG_IS_VECTOR_EDIT (self));
	g_return_if_fail (step >= 0.0);

	if (self->step == step)
		return;
	self->step = step;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STEP]);
}

guint
lrg_vector_edit_get_digits (LrgVectorEdit *self)
{
	g_return_val_if_fail (LRG_IS_VECTOR_EDIT (self), 0);
	return self->digits;
}

void
lrg_vector_edit_set_digits (LrgVectorEdit *self,
                            guint          digits)
{
	g_return_if_fail (LRG_IS_VECTOR_EDIT (self));

	digits = MIN (digits, 9u);
	if (self->digits == digits)
		return;
	self->digits = digits;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DIGITS]);
}

gboolean
lrg_vector_edit_get_uniform (LrgVectorEdit *self)
{
	g_return_val_if_fail (LRG_IS_VECTOR_EDIT (self), FALSE);
	return self->uniform;
}

void
lrg_vector_edit_set_uniform (LrgVectorEdit *self,
                             gboolean       uniform)
{
	g_return_if_fail (LRG_IS_VECTOR_EDIT (self));

	uniform = !!uniform;
	if (self->uniform == uniform)
		return;
	self->uniform = uniform;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNIFORM]);
}

gboolean
lrg_vector_edit_get_show_uniform (LrgVectorEdit *self)
{
	g_return_val_if_fail (LRG_IS_VECTOR_EDIT (self), FALSE);
	return self->show_uniform;
}

void
lrg_vector_edit_set_show_uniform (LrgVectorEdit *self,
                                  gboolean       show)
{
	g_return_if_fail (LRG_IS_VECTOR_EDIT (self));

	show = !!show;
	if (self->show_uniform == show)
		return;
	self->show_uniform = show;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SHOW_UNIFORM]);
}

gboolean
lrg_vector_edit_get_axis_mixed (LrgVectorEdit *self,
                                guint          axis)
{
	g_return_val_if_fail (LRG_IS_VECTOR_EDIT (self), FALSE);
	g_return_val_if_fail (axis < LRG_VECTOR_EDIT_MAX_COMPONENTS, FALSE);
	return self->mixed[axis];
}

void
lrg_vector_edit_set_axis_mixed (LrgVectorEdit *self,
                                guint          axis,
                                gboolean       mixed)
{
	g_return_if_fail (LRG_IS_VECTOR_EDIT (self));
	g_return_if_fail (axis < LRG_VECTOR_EDIT_MAX_COMPONENTS);
	self->mixed[axis] = !!mixed;
}

gint
lrg_vector_edit_get_editing_axis (LrgVectorEdit *self)
{
	g_return_val_if_fail (LRG_IS_VECTOR_EDIT (self), -1);
	return self->edit_axis;
}
