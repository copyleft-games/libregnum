/* lrg-toast.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgToast: A short notification card.
 *
 * A card (POPUP part) with a coloured stripe for its kind (INDICATOR
 * part), a title (LABEL part), a muted, word-wrapped message and a thin
 * hairline (FILL part) that shrinks as the toast's time runs out. The
 * toast keeps its own clock (lrg_toast_advance()); LrgToastQueue drives
 * it and animates the card in and out.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-toast.h"
#include "../lrg-log.h"

/* Card width: one width for every card so stacks line up */
#define TOAST_WIDTH (340.0f)

/* Kind stripe width */
#define TOAST_STRIPE (4.0f)

/* Gap between title and message */
#define TOAST_TEXT_GAP (4.0f)

/* Progress hairline thickness */
#define TOAST_HAIRLINE (2.0f)

struct _LrgToast
{
	LrgWidget     parent_instance;

	gchar        *title;
	gchar        *message;
	LrgToastKind  kind;
	gdouble       duration;
	gdouble       elapsed;
	gboolean      dismissed;

	/* Wrapped message, valid for wrap_width */
	GPtrArray    *lines;
	gfloat        wrap_width;
};

G_DEFINE_FINAL_TYPE (LrgToast, lrg_toast, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_TITLE,
	PROP_MESSAGE,
	PROP_KIND,
	PROP_DURATION,
	PROP_DISMISSED,
	N_PROPS
};

enum
{
	SIGNAL_DISMISSED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

GType
lrg_toast_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TOAST_KIND_INFO, "LRG_TOAST_KIND_INFO", "info" },
			{ LRG_TOAST_KIND_SUCCESS, "LRG_TOAST_KIND_SUCCESS", "success" },
			{ LRG_TOAST_KIND_WARNING, "LRG_TOAST_KIND_WARNING", "warning" },
			{ LRG_TOAST_KIND_DANGER, "LRG_TOAST_KIND_DANGER", "danger" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgToastKind"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * Layout helpers
 * ========================================================================== */

typedef struct
{
	gfloat pad;
	gfloat lead;          /* text column left offset */
	gfloat title_size;
	gfloat message_size;
	gfloat title_h;
	gfloat line_h;
} ToastMetrics;

static void
toast_metrics (LrgToast     *self,
               ToastMetrics *m)
{
	LrgTheme *theme = lrg_widget_get_theme (LRG_WIDGET (self));
	GrlFont  *font = lrg_theme_get_ui_font (theme);

	m->pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	m->lead = m->pad + TOAST_STRIPE + m->pad * 0.75f;
	m->title_size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	m->message_size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE_SMALL);
	m->title_h = 0.0f;
	if (self->title != NULL && self->title[0] != '\0')
		lrg_paint_measure_text (font, self->title, m->title_size, 1.0f, NULL, &m->title_h);
	lrg_paint_measure_text (font, "Ag", m->message_size, 1.0f, NULL, &m->line_h);
	m->line_h = ceilf (m->line_h * 1.25f);
}

/* text_w: width of @text at @size in the toast's font */
static gfloat
text_w (LrgToast    *self,
        const gchar *text,
        gfloat       size)
{
	gfloat w = 0.0f;

	if (text == NULL || text[0] == '\0')
		return 0.0f;
	lrg_paint_measure_text (lrg_theme_get_ui_font (lrg_widget_get_theme (LRG_WIDGET (self))), text, size,
	                        1.0f, &w, NULL);
	return w;
}

/*
 * wrap_message:
 *
 * Fills self->lines with the message broken at '\n' and word-wrapped
 * to @max_w; cached until the width or the text changes. Returns the
 * widest line.
 */
static gfloat
wrap_message (LrgToast *self,
              gfloat    size,
              gfloat    max_w)
{
	g_auto(GStrv) paragraphs = NULL;
	gfloat        widest = 0.0f;
	guint         p;
	guint         i;

	g_ptr_array_set_size (self->lines, 0);
	self->wrap_width = max_w;
	if (self->message == NULL || self->message[0] == '\0')
		return 0.0f;

	paragraphs = g_strsplit (self->message, "\n", -1);
	for (p = 0; paragraphs[p] != NULL; p++)
	{
		g_auto(GStrv)      words = g_strsplit (paragraphs[p], " ", -1);
		g_autoptr(GString) line = g_string_new (NULL);

		for (i = 0; words[i] != NULL; i++)
		{
			g_autofree gchar *candidate = NULL;

			if (words[i][0] == '\0' && line->len > 0)
				continue;
			candidate = line->len > 0 ? g_strconcat (line->str, " ", words[i], NULL)
			                          : g_strdup (words[i]);
			if (line->len > 0 && text_w (self, candidate, size) > max_w)
			{
				g_ptr_array_add (self->lines, g_strdup (line->str));
				g_string_assign (line, words[i]);
			}
			else
			{
				g_string_assign (line, candidate);
			}
		}
		g_ptr_array_add (self->lines, g_strdup (line->str));
	}

	for (i = 0; i < self->lines->len; i++)
		widest = MAX (widest, text_w (self, g_ptr_array_index (self->lines, i), size));
	return widest;
}

/* kind_color: the theme colour role for the toast's kind */
static LrgThemeColor
kind_color (LrgToastKind kind)
{
	switch (kind)
	{
	case LRG_TOAST_KIND_SUCCESS:
		return LRG_THEME_COLOR_SUCCESS;
	case LRG_TOAST_KIND_WARNING:
		return LRG_THEME_COLOR_WARNING;
	case LRG_TOAST_KIND_DANGER:
		return LRG_THEME_COLOR_DANGER;
	case LRG_TOAST_KIND_INFO:
	default:
		return LRG_THEME_COLOR_ACCENT;
	}
}

/* ==========================================================================
 * Widget vfuncs
 * ========================================================================== */

static void
lrg_toast_style_defaults (LrgWidget      *widget,
                          LrgUIPart       part,
                          LrgWidgetState  state,
                          LrgPartLook    *look)
{
	LrgToast *self = LRG_TOAST (widget);
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_POPUP:
		/* The card lifts a little under the pointer */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS));
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) *
		                          ((state & LRG_WIDGET_STATE_HOVERED) ? 1.5f : 1.0f),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y) *
		                          ((state & LRG_WIDGET_STATE_HOVERED) ? 1.5f : 1.0f));
		break;

	case LRG_UI_PART_INDICATOR:
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, kind_color (self->kind)));
		lrg_part_look_set_radius (look, G_MAXFLOAT);
		break;

	case LRG_UI_PART_FILL:
		{
			GrlColor c;

			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, kind_color (self->kind)), 0.7f, &c);
			lrg_part_look_set_background (look, &c);
			lrg_part_look_set_radius (look, G_MAXFLOAT);
		}
		break;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		break;

	case LRG_UI_PART_ITEM:
		/* The message text */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		break;

	default:
		LRG_WIDGET_CLASS (lrg_toast_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_toast_measure (LrgWidget *widget,
                   gfloat    *width,
                   gfloat    *height)
{
	LrgToast     *self = LRG_TOAST (widget);
	ToastMetrics  m;
	gfloat        inner_max;
	gfloat        h;

	toast_metrics (self, &m);
	inner_max = TOAST_WIDTH - m.lead - m.pad;

	wrap_message (self, m.message_size, inner_max);
	*width = TOAST_WIDTH;

	h = m.pad * 2.0f + m.title_h;
	if (self->lines->len > 0)
		h += (m.title_h > 0.0f ? TOAST_TEXT_GAP : 0.0f) + m.line_h * (gfloat) self->lines->len;
	*height = ceilf (h + TOAST_HAIRLINE);
}

static void
lrg_toast_paint (LrgWidget    *widget,
                 LrgPaintList *list)
{
	LrgToast     *self = LRG_TOAST (widget);
	GrlFont      *font = lrg_theme_get_ui_font (lrg_widget_get_theme (widget));
	ToastMetrics  m;
	LrgPartLook   card;
	LrgPartLook   stripe;
	LrgPartLook   title;
	LrgPartLook   message;
	gfloat        x = lrg_widget_get_world_x (widget);
	gfloat        y = lrg_widget_get_world_y (widget);
	gfloat        w = lrg_widget_get_width (widget);
	gfloat        h = lrg_widget_get_height (widget);
	gfloat        ty;
	guint         i;

	toast_metrics (self, &m);

	/* Card */
	lrg_widget_resolve_look (widget, LRG_UI_PART_POPUP, &card);
	lrg_widget_paint_box (widget, list, &card, x, y, w, h);

	/* Kind stripe down the left edge */
	lrg_widget_resolve_look (widget, LRG_UI_PART_INDICATOR, &stripe);
	lrg_widget_paint_box (widget, list, &stripe, x + m.pad, y + m.pad, TOAST_STRIPE,
	                      MAX (h - m.pad * 2.0f - TOAST_HAIRLINE, TOAST_STRIPE));

	/* Title and message, clipped to the text column */
	lrg_paint_list_push_clip (list, x + m.lead, y, MAX (w - m.lead - m.pad, 0.0f), h);
	ty = y + m.pad;
	if (self->title != NULL && self->title[0] != '\0')
	{
		lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &title);
		lrg_paint_list_text (list, font, self->title, x + m.lead, ty, m.title_size, 1.0f, &title.foreground);
		ty += m.title_h + TOAST_TEXT_GAP;
	}
	if (self->wrap_width != w - m.lead - m.pad)
		wrap_message (self, m.message_size, w - m.lead - m.pad);
	lrg_widget_resolve_look (widget, LRG_UI_PART_ITEM, &message);
	for (i = 0; i < self->lines->len; i++)
		lrg_paint_list_text (list, font, g_ptr_array_index (self->lines, i), x + m.lead,
		                     ty + m.line_h * (gfloat) i, m.message_size, 1.0f, &message.foreground);
	lrg_paint_list_pop_clip (list);

	/* Remaining-time hairline along the bottom, inside the corners */
	if (self->duration > 0.0)
	{
		LrgPartLook fill;
		gfloat      inset = MIN (card.radii.bottom_left, w * 0.25f) + 2.0f;
		gfloat      full = MAX (w - inset * 2.0f, 0.0f);
		gfloat      left = full * lrg_toast_get_remaining_fraction (self);

		gfloat      t;

		lrg_widget_resolve_look (widget, LRG_UI_PART_FILL, &fill);
		t = fill.size > 0.0f ? fill.size : TOAST_HAIRLINE;
		if (left > 0.5f)
			lrg_widget_paint_box (widget, list, &fill, x + inset, y + h - TOAST_HAIRLINE - t, left, t);
	}
}

static gboolean
lrg_toast_handle_event (LrgWidget        *widget,
                        const LrgUIEvent *event)
{
	LrgToast *self = LRG_TOAST (widget);
	gboolean  inside = lrg_widget_contains_point (widget, lrg_ui_event_get_x (event),
	                                              lrg_ui_event_get_y (event));

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return lrg_ui_event_get_button (event) == 0 && inside;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		/* A click dismisses early */
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (inside)
			lrg_toast_dismiss (self);
		return TRUE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_toast_finalize (GObject *object)
{
	LrgToast *self = LRG_TOAST (object);

	g_clear_pointer (&self->title, g_free);
	g_clear_pointer (&self->message, g_free);
	g_clear_pointer (&self->lines, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_toast_parent_class)->finalize (object);
}

static void
lrg_toast_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgToast *self = LRG_TOAST (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		g_value_set_string (value, self->title);
		break;
	case PROP_MESSAGE:
		g_value_set_string (value, self->message);
		break;
	case PROP_KIND:
		g_value_set_enum (value, self->kind);
		break;
	case PROP_DURATION:
		g_value_set_double (value, self->duration);
		break;
	case PROP_DISMISSED:
		g_value_set_boolean (value, self->dismissed);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_toast_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgToast *self = LRG_TOAST (object);

	switch (prop_id)
	{
	case PROP_TITLE:
		lrg_toast_set_title (self, g_value_get_string (value));
		break;
	case PROP_MESSAGE:
		lrg_toast_set_message (self, g_value_get_string (value));
		break;
	case PROP_KIND:
		lrg_toast_set_kind (self, (LrgToastKind) g_value_get_enum (value));
		break;
	case PROP_DURATION:
		lrg_toast_set_duration (self, g_value_get_double (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_toast_class_init (LrgToastClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->finalize = lrg_toast_finalize;
	object_class->get_property = lrg_toast_get_property;
	object_class->set_property = lrg_toast_set_property;

	widget_class->style_defaults = lrg_toast_style_defaults;
	widget_class->measure = lrg_toast_measure;
	widget_class->paint = lrg_toast_paint;
	widget_class->handle_event = lrg_toast_handle_event;

	/**
	 * LrgToast:title:
	 *
	 * First line, in the primary text colour.
	 */
	properties[PROP_TITLE] =
		g_param_spec_string ("title", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgToast:message:
	 *
	 * Detail text in the muted colour, word-wrapped to the card.
	 */
	properties[PROP_MESSAGE] =
		g_param_spec_string ("message", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgToast:kind:
	 *
	 * Picks the accent colour of the stripe and the time hairline.
	 */
	properties[PROP_KIND] =
		g_param_spec_enum ("kind", NULL, NULL, LRG_TYPE_TOAST_KIND, LRG_TOAST_KIND_INFO,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgToast:duration:
	 *
	 * Seconds the toast stays up; 0 keeps it until it is dismissed.
	 */
	properties[PROP_DURATION] =
		g_param_spec_double ("duration", NULL, NULL, 0.0, G_MAXDOUBLE, LRG_TOAST_DEFAULT_DURATION,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgToast:dismissed:
	 *
	 * Whether the toast expired or was dismissed.
	 */
	properties[PROP_DISMISSED] =
		g_param_spec_boolean ("dismissed", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgToast::dismissed:
	 * @self: the toast
	 *
	 * Emitted once, when the toast's time runs out, it is clicked, or
	 * lrg_toast_dismiss() is called.
	 */
	signals[SIGNAL_DISMISSED] =
		g_signal_new ("dismissed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_toast_init (LrgToast *self)
{
	self->kind = LRG_TOAST_KIND_INFO;
	self->duration = LRG_TOAST_DEFAULT_DURATION;
	self->lines = g_ptr_array_new_with_free_func (g_free);
	self->wrap_width = -1.0f;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgToast *
lrg_toast_new (const gchar *title,
               const gchar *message)
{
	return g_object_new (LRG_TYPE_TOAST, "title", title, "message", message, NULL);
}

const gchar *
lrg_toast_get_title (LrgToast *self)
{
	g_return_val_if_fail (LRG_IS_TOAST (self), NULL);

	return self->title;
}

void
lrg_toast_set_title (LrgToast    *self,
                     const gchar *title)
{
	g_return_if_fail (LRG_IS_TOAST (self));

	if (g_strcmp0 (self->title, title) == 0)
		return;
	g_free (self->title);
	self->title = g_strdup (title);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TITLE]);
}

const gchar *
lrg_toast_get_message (LrgToast *self)
{
	g_return_val_if_fail (LRG_IS_TOAST (self), NULL);

	return self->message;
}

void
lrg_toast_set_message (LrgToast    *self,
                       const gchar *message)
{
	g_return_if_fail (LRG_IS_TOAST (self));

	if (g_strcmp0 (self->message, message) == 0)
		return;
	g_free (self->message);
	self->message = g_strdup (message);
	self->wrap_width = -1.0f;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MESSAGE]);
}

LrgToastKind
lrg_toast_get_kind (LrgToast *self)
{
	g_return_val_if_fail (LRG_IS_TOAST (self), LRG_TOAST_KIND_INFO);

	return self->kind;
}

void
lrg_toast_set_kind (LrgToast     *self,
                    LrgToastKind  kind)
{
	g_return_if_fail (LRG_IS_TOAST (self));

	if (self->kind == kind)
		return;
	self->kind = kind;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KIND]);
}

gdouble
lrg_toast_get_duration (LrgToast *self)
{
	g_return_val_if_fail (LRG_IS_TOAST (self), 0.0);

	return self->duration;
}

void
lrg_toast_set_duration (LrgToast *self,
                        gdouble   duration)
{
	g_return_if_fail (LRG_IS_TOAST (self));

	duration = MAX (duration, 0.0);
	if (self->duration == duration)
		return;
	self->duration = duration;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
}

gdouble
lrg_toast_get_elapsed (LrgToast *self)
{
	g_return_val_if_fail (LRG_IS_TOAST (self), 0.0);

	return self->elapsed;
}

gfloat
lrg_toast_get_remaining_fraction (LrgToast *self)
{
	g_return_val_if_fail (LRG_IS_TOAST (self), 0.0f);

	if (self->duration <= 0.0)
		return 1.0f;
	return (gfloat) CLAMP (1.0 - self->elapsed / self->duration, 0.0, 1.0);
}

gboolean
lrg_toast_advance (LrgToast *self,
                   gdouble   seconds)
{
	g_return_val_if_fail (LRG_IS_TOAST (self), TRUE);

	if (self->dismissed)
		return TRUE;

	/* Reading it: hold the clock while the pointer is on the card */
	if (seconds > 0.0 && !(lrg_widget_get_state_flags (LRG_WIDGET (self)) & LRG_WIDGET_STATE_HOVERED))
		self->elapsed += seconds;

	if (self->duration > 0.0 && self->elapsed >= self->duration)
		lrg_toast_dismiss (self);
	return self->dismissed;
}

void
lrg_toast_dismiss (LrgToast *self)
{
	g_return_if_fail (LRG_IS_TOAST (self));

	if (self->dismissed)
		return;
	self->dismissed = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DISMISSED]);
	g_signal_emit (self, signals[SIGNAL_DISMISSED], 0);
}

gboolean
lrg_toast_get_dismissed (LrgToast *self)
{
	g_return_val_if_fail (LRG_IS_TOAST (self), TRUE);

	return self->dismissed;
}
