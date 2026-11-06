/* lrg-breadcrumb.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBreadcrumb: A path of clickable segments that collapses to an ellipsis when narrow.
 *
 * The breadcrumb is one widget that lays its segments out itself into
 * "slots" (a segment or the "…" ellipsis), separated by small chevrons.
 * Each slot has its own hover spring, so the ITEM look of one segment
 * glides independently of the others; the keyboard moves a segment
 * cursor (drawn with the INDICATOR part) with Left / Right.
 *
 * When the segments do not fit the allocated width, middle segments are
 * folded into the ellipsis, keeping the first and as many trailing
 * segments as fit. Clicking the ellipsis opens a popup list of the
 * hidden segments (a private popup container with one focusable row
 * per hidden segment, the same pattern as LrgComboBox).
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-breadcrumb.h"
#include "lrg-container.h"
#include "lrg-canvas.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Keys for slots: a segment index, the ellipsis, or nothing */
#define KEY_NONE     (-2)
#define KEY_ELLIPSIS (LRG_BREADCRUMB_ELLIPSIS)

/* Inset between the popup edge and its rows */
#define CRUMB_POPUP_PAD (4.0f)

/* The ellipsis label (U+2026) */
#define CRUMB_ELLIPSIS_TEXT "\xe2\x80\xa6"

typedef struct
{
	gchar          *label;
	gpointer        data;
	GDestroyNotify  destroy;
	LrgSpring       hover;
} Segment;

typedef struct
{
	gint    key;        /* segment index or KEY_ELLIPSIS */
	gfloat  x;          /* relative to the widget */
	gfloat  width;
	gchar  *display;    /* shortened label, NULL = the full label */
} Slot;

#define LRG_TYPE_CRUMB_POPUP (lrg_crumb_popup_get_type ())
G_DECLARE_FINAL_TYPE (LrgCrumbPopup, lrg_crumb_popup, LRG, CRUMB_POPUP, LrgContainer)

#define LRG_TYPE_CRUMB_ROW (lrg_crumb_row_get_type ())
G_DECLARE_FINAL_TYPE (LrgCrumbRow, lrg_crumb_row, LRG, CRUMB_ROW, LrgWidget)

struct _LrgBreadcrumb
{
	LrgWidget      parent_instance;

	GArray        *segments;       /* Segment */
	GArray        *slots;          /* Slot, rebuilt by relayout() */
	gfloat         laid_width;     /* width the slots were built for, -1 = stale */
	guint          hidden_first;
	guint          hidden_count;

	gint           hover_key;
	gint           press_key;
	gint           focus_key;
	gboolean       swallow_press;  /* the press that closed the list */
	gboolean       closing;        /* popdown() in progress */
	LrgSpring      ellipsis_hover;
	gboolean       stepped;

	/* Hidden-segment list */
	LrgCrumbPopup *popup;
	LrgCanvas     *canvas;         /* weak */
	gulong         dismissed_id;
};

struct _LrgCrumbPopup
{
	LrgContainer   parent_instance;

	LrgBreadcrumb *crumb;          /* weak */
};

struct _LrgCrumbRow
{
	LrgWidget  parent_instance;

	guint      index;              /* segment index */
};

G_DEFINE_FINAL_TYPE (LrgBreadcrumb, lrg_breadcrumb, LRG_TYPE_WIDGET)
G_DEFINE_FINAL_TYPE (LrgCrumbPopup, lrg_crumb_popup, LRG_TYPE_CONTAINER)
G_DEFINE_FINAL_TYPE (LrgCrumbRow, lrg_crumb_row, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_N_SEGMENTS,
	PROP_COLLAPSED,
	PROP_POPUP_OPEN,
	N_PROPS
};

enum
{
	SIGNAL_ACTIVATED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void crumb_activate_from_row (LrgBreadcrumb *self,
                                     guint          index,
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
font_size (LrgWidget *widget)
{
	return lrg_theme_get_metric (lrg_widget_get_theme (widget), LRG_THEME_METRIC_FONT_SIZE);
}

static gfloat
text_width (LrgWidget   *widget,
            const gchar *text)
{
	gfloat w = 0.0f;

	if (text == NULL || text[0] == '\0')
		return 0.0f;
	lrg_paint_measure_text (lrg_theme_get_ui_font (lrg_widget_get_theme (widget)), text,
	                        font_size (widget), 1.0f, &w, NULL);
	return w;
}

static void
paint_text_centered_v (LrgPaintList   *list,
                       LrgWidget      *widget,
                       const gchar    *text,
                       gfloat          x,
                       gfloat          cy,
                       const GrlColor *color)
{
	GrlFont *font = lrg_theme_get_ui_font (lrg_widget_get_theme (widget));
	gfloat   size = font_size (widget);
	gfloat   th = size;

	if (text == NULL || text[0] == '\0')
		return;
	lrg_paint_measure_text (font, text, size, 1.0f, NULL, &th);
	lrg_paint_list_text (list, font, text, roundf (x), roundf (cy - th * 0.5f), size, 1.0f, color);
}

/* item_pad: horizontal padding inside a segment */
static gfloat
item_pad (LrgWidget *widget)
{
	return roundf (lrg_theme_get_metric (lrg_widget_get_theme (widget), LRG_THEME_METRIC_PADDING_X) * 0.5f);
}

/* separator_width: room for one chevron between segments */
static gfloat
separator_width (LrgWidget *widget)
{
	return roundf (font_size (widget) * 1.0f);
}

static Segment *
segment_at (LrgBreadcrumb *self,
            guint          index)
{
	return &g_array_index (self->segments, Segment, index);
}

static void
segment_clear (gpointer data)
{
	Segment *s = data;

	g_clear_pointer (&s->label, g_free);
	if (s->destroy != NULL && s->data != NULL)
		s->destroy (s->data);
	s->data = NULL;
	s->destroy = NULL;
}

static void
slot_clear (gpointer data)
{
	Slot *s = data;

	g_clear_pointer (&s->display, g_free);
}

/* segment_width: full width of segment @index with its padding */
static gfloat
segment_width (LrgBreadcrumb *self,
               guint          index)
{
	return text_width (LRG_WIDGET (self), segment_at (self, index)->label) + item_pad (LRG_WIDGET (self)) * 2.0f;
}

/* ellipsis_width: width of the "…" slot */
static gfloat
ellipsis_width (LrgBreadcrumb *self)
{
	return text_width (LRG_WIDGET (self), CRUMB_ELLIPSIS_TEXT) + item_pad (LRG_WIDGET (self)) * 2.0f;
}

/*
 * shorten:
 *
 * The longest prefix of @label that, followed by "…", fits in @avail
 * (text only), or "…" alone.
 */
static gchar *
shorten (LrgBreadcrumb *self,
         const gchar   *label,
         gfloat         avail)
{
	glong n = g_utf8_strlen (label, -1);

	while (n > 0)
	{
		g_autofree gchar *prefix = g_utf8_substring (label, 0, n);
		g_autofree gchar *candidate = g_strconcat (prefix, CRUMB_ELLIPSIS_TEXT, NULL);

		if (text_width (LRG_WIDGET (self), candidate) <= avail)
			return g_steal_pointer (&candidate);
		n--;
	}
	return g_strdup (CRUMB_ELLIPSIS_TEXT);
}

/* add_slot: append a slot at x */
static gfloat
add_slot (LrgBreadcrumb *self,
          gint           key,
          gfloat         x,
          gfloat         width)
{
	Slot s;

	s.key = key;
	s.x = x;
	s.width = width;
	s.display = NULL;
	g_array_append_val (self->slots, s);
	return x + width;
}

/*
 * relayout:
 *
 * Builds the slots for the current width. Everything fits: one slot per
 * segment. Otherwise the first segment, an ellipsis and as many trailing
 * segments as fit (at least the last); if even that is too wide the
 * first segment folds in too, and the last label is shortened with "…".
 */
static void
relayout (LrgBreadcrumb *self)
{
	LrgWidget *widget = LRG_WIDGET (self);
	gfloat     width = lrg_widget_get_width (widget);
	gfloat     sep = separator_width (widget);
	gfloat     total = 0.0f;
	guint      n = self->segments->len;
	gboolean   was_collapsed = self->hidden_count > 0;
	guint      i;

	g_array_set_size (self->slots, 0);
	self->laid_width = width;
	self->hidden_first = 0;
	self->hidden_count = 0;

	if (n == 0)
		goto done;

	for (i = 0; i < n; i++)
		total += segment_width (self, i) + (i > 0 ? sep : 0.0f);

	/* Half a unit of slack: parents snap slots to whole pixels, so a
	 * natural width of 251.2 may be allocated as 251 */
	if (total <= width + 0.5f || n == 1)
	{
		gfloat x = 0.0f;

		for (i = 0; i < n; i++)
		{
			if (i > 0)
				x += sep;
			x = add_slot (self, (gint) i, x, segment_width (self, i));
		}
	}
	else
	{
		gfloat   ell = ellipsis_width (self);
		gfloat   used;
		guint    keep_from = n - 1;     /* first trailing segment kept */
		gboolean keep_first = TRUE;
		gfloat   x = 0.0f;

		/* first, …, last */
		used = segment_width (self, 0) + sep + ell + sep + segment_width (self, n - 1);
		if (n == 2 || used > width)
		{
			/* Not even that: "…, last" */
			keep_first = n > 2 ? FALSE : TRUE;
			if (n > 2)
				used = ell + sep + segment_width (self, n - 1);
		}

		/* Bring trailing segments back while they fit */
		while (keep_from > (keep_first ? 2u : 1u) &&
		       used + segment_width (self, keep_from - 1) + sep <= width)
		{
			used += segment_width (self, keep_from - 1) + sep;
			keep_from--;
		}

		if (n == 2)
		{
			/* Nothing to fold: the last label is shortened below */
			x = add_slot (self, 0, x, segment_width (self, 0));
			x += sep;
		}
		else
		{
			if (keep_first)
			{
				x = add_slot (self, 0, x, segment_width (self, 0));
				x += sep;
			}
			self->hidden_first = keep_first ? 1 : 0;
			self->hidden_count = keep_from - self->hidden_first;
			x = add_slot (self, KEY_ELLIPSIS, x, ell);
			x += sep;
		}

		for (i = keep_from; i < n; i++)
		{
			if (i > keep_from)
				x += sep;
			x = add_slot (self, (gint) i, x, segment_width (self, i));
		}
	}

	/* Still too wide: shorten the last label to what is left */
	if (self->slots->len > 0)
	{
		Slot *last = &g_array_index (self->slots, Slot, self->slots->len - 1);

		if (last->x + last->width > width && last->key >= 0)
		{
			gfloat avail = MAX (0.0f, width - last->x - item_pad (widget) * 2.0f);

			last->display = shorten (self, segment_at (self, (guint) last->key)->label, avail);
			last->width = MAX (0.0f, width - last->x);
		}
	}

done:
	if (was_collapsed != (self->hidden_count > 0))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_COLLAPSED]);
}

/* ensure_layout: rebuild the slots if the width changed */
static void
ensure_layout (LrgBreadcrumb *self)
{
	if (self->laid_width != lrg_widget_get_width (LRG_WIDGET (self)))
		relayout (self);
}

/* invalidate: segments changed; rebuild before the next use */
static void
invalidate (LrgBreadcrumb *self)
{
	self->laid_width = -1.0f;
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* find_slot: the slot showing @key, or NULL */
static Slot *
find_slot (LrgBreadcrumb *self,
           gint           key)
{
	guint i;

	ensure_layout (self);
	for (i = 0; i < self->slots->len; i++)
		if (g_array_index (self->slots, Slot, i).key == key)
			return &g_array_index (self->slots, Slot, i);
	return NULL;
}

/* slot_index_of: position of @key among the slots, or -1 */
static gint
slot_index_of (LrgBreadcrumb *self,
               gint           key)
{
	guint i;

	ensure_layout (self);
	for (i = 0; i < self->slots->len; i++)
		if (g_array_index (self->slots, Slot, i).key == key)
			return (gint) i;
	return -1;
}

/* key_at: the slot key under canvas point x, KEY_NONE between slots */
/*
 * slot_left:
 *
 * Left edge of @slot relative to the breadcrumb. Slots are laid out
 * from the left; in right-to-left the trail is mirrored so it reads
 * from the right edge.
 */
static gfloat
slot_left (LrgBreadcrumb *self,
           const Slot    *slot)
{
	if (lrg_widget_is_rtl (LRG_WIDGET (self)))
		return lrg_widget_get_width (LRG_WIDGET (self)) - slot->x - slot->width;
	return slot->x;
}

static gint
key_at (LrgBreadcrumb *self,
        gfloat         px)
{
	gfloat rel = px - lrg_widget_get_world_x (LRG_WIDGET (self));
	guint  i;

	ensure_layout (self);
	for (i = 0; i < self->slots->len; i++)
	{
		Slot *s = &g_array_index (self->slots, Slot, i);

		gfloat left = slot_left (self, s);

		if (rel >= left && rel < left + s->width)
			return s->key;
	}
	return KEY_NONE;
}

/* key_spring: the hover spring of slot @key */
static LrgSpring *
key_spring (LrgBreadcrumb *self,
            gint           key)
{
	if (key == KEY_ELLIPSIS)
		return &self->ellipsis_hover;
	if (key >= 0 && (guint) key < self->segments->len)
		return &segment_at (self, (guint) key)->hover;
	return NULL;
}

/* key_highlighted: the slot looks hovered (pointer, keyboard, open list) */
static gboolean
key_highlighted (LrgBreadcrumb *self,
                 gint           key)
{
	LrgWidgetState state = lrg_widget_get_state_flags (LRG_WIDGET (self));

	if (key == self->hover_key)
		return TRUE;
	if (key == self->focus_key && (state & LRG_WIDGET_STATE_FOCUS_VISIBLE))
		return TRUE;
	return key == KEY_ELLIPSIS && self->popup != NULL;
}

/* ==========================================================================
 * LrgCrumbRow / LrgCrumbPopup: the list of hidden segments
 * ========================================================================== */

static LrgBreadcrumb *
row_crumb (LrgCrumbRow *row)
{
	LrgContainer *parent = lrg_widget_get_parent (LRG_WIDGET (row));

	return LRG_IS_CRUMB_POPUP (parent) ? LRG_CRUMB_POPUP (parent)->crumb : NULL;
}

static void
lrg_crumb_row_style_defaults (LrgWidget      *widget,
                              LrgUIPart       part,
                              LrgWidgetState  state,
                              LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_ITEM:
		{
			const GrlColor *hover = lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
			GrlColor        rest = transparent (hover);

			if (state & LRG_WIDGET_STATE_PRESSED)
				lrg_part_look_set_background (look, lrg_theme_get_color (theme,
				                                                         LRG_THEME_COLOR_CONTROL_PRESSED));
			else if (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_FOCUS_VISIBLE))
				lrg_part_look_set_background (look, hover);
			else
				lrg_part_look_set_background (look, &rest);
			lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
			lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
		}
		break;

	case LRG_UI_PART_FOCUS:
		{
			GrlColor none = transparent (lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS));

			/* The highlighted row is the focus indicator */
			lrg_part_look_set_border (look, &none, 0.0f);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_crumb_row_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_crumb_row_measure (LrgWidget *widget,
                       gfloat    *width,
                       gfloat    *height)
{
	LrgCrumbRow   *self = LRG_CRUMB_ROW (widget);
	LrgBreadcrumb *crumb = row_crumb (self);
	LrgTheme      *theme = lrg_widget_get_theme (widget);
	gfloat         pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);

	*width = pad * 2.0f + (crumb != NULL ? text_width (widget, lrg_breadcrumb_get_segment_label (crumb,
	                                                                                             self->index))
	                                      : 0.0f);
	*height = lrg_theme_get_metric (theme, LRG_THEME_METRIC_ROW_HEIGHT);
}

static void
lrg_crumb_row_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	LrgCrumbRow   *self = LRG_CRUMB_ROW (widget);
	LrgBreadcrumb *crumb = row_crumb (self);
	LrgTheme      *theme = lrg_widget_get_theme (widget);
	LrgPartLook    item;
	gfloat         x = lrg_widget_get_world_x (widget);
	gfloat         y = lrg_widget_get_world_y (widget);
	gfloat         w = lrg_widget_get_width (widget);
	gfloat         h = lrg_widget_get_height (widget);

	if (crumb == NULL)
		return;

	lrg_widget_resolve_look (widget, LRG_UI_PART_ITEM, &item);
	lrg_widget_paint_box (widget, list, &item, x, y, w, h);
	paint_text_centered_v (list, widget, lrg_breadcrumb_get_segment_label (crumb, self->index),
	                       x + lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X), y + h * 0.5f,
	                       &item.foreground);
}

static gboolean
lrg_crumb_row_handle_event (LrgWidget        *widget,
                            const LrgUIEvent *event)
{
	LrgCrumbRow   *self = LRG_CRUMB_ROW (widget);
	LrgBreadcrumb *crumb = row_crumb (self);

	if (crumb == NULL)
		return FALSE;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return lrg_ui_event_get_button (event) == 0;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (lrg_widget_contains_point (widget, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event)))
			crumb_activate_from_row (crumb, self->index, FALSE);
		return TRUE;

	case LRG_UI_EVENT_ACTIVATE:
		crumb_activate_from_row (crumb, self->index, TRUE);
		return TRUE;

	default:
		return FALSE;
	}
}

static void
lrg_crumb_row_class_init (LrgCrumbRowClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	widget_class->style_defaults = lrg_crumb_row_style_defaults;
	widget_class->measure = lrg_crumb_row_measure;
	widget_class->paint = lrg_crumb_row_paint;
	widget_class->handle_event = lrg_crumb_row_handle_event;
}

static void
lrg_crumb_row_init (LrgCrumbRow *self)
{
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

static void
lrg_crumb_popup_dispose (GObject *object)
{
	LrgCrumbPopup *self = LRG_CRUMB_POPUP (object);

	g_clear_weak_pointer (&self->crumb);

	G_OBJECT_CLASS (lrg_crumb_popup_parent_class)->dispose (object);
}

static void
lrg_crumb_popup_style_defaults (LrgWidget      *widget,
                                LrgUIPart       part,
                                LrgWidgetState  state,
                                LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	if (part == LRG_UI_PART_POPUP)
	{
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_POPUP));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_POPUP_RADIUS));
		lrg_part_look_set_shadow (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_OFFSET_Y));
		return;
	}
	LRG_WIDGET_CLASS (lrg_crumb_popup_parent_class)->style_defaults (widget, part, state, look);
}

static void
lrg_crumb_popup_measure (LrgWidget *widget,
                         gfloat    *width,
                         gfloat    *height)
{
	gfloat  w = 0.0f;
	gfloat  h = 0.0f;
	GList  *l;

	for (l = lrg_container_get_children (LRG_CONTAINER (widget)); l != NULL; l = l->next)
	{
		gfloat rw;
		gfloat rh;

		lrg_widget_get_preferred_size (LRG_WIDGET (l->data), &rw, &rh);
		w = MAX (w, rw);
		h += rh;
	}
	*width = w + CRUMB_POPUP_PAD * 2.0f;
	*height = h + CRUMB_POPUP_PAD * 2.0f;
}

static void
lrg_crumb_popup_layout_children (LrgContainer *container)
{
	LrgWidget *widget = LRG_WIDGET (container);
	gfloat     row_h = lrg_theme_get_metric (lrg_widget_get_theme (widget), LRG_THEME_METRIC_ROW_HEIGHT);
	gfloat     w = MAX (0.0f, lrg_widget_get_width (widget) - CRUMB_POPUP_PAD * 2.0f);
	gfloat     y = CRUMB_POPUP_PAD;
	GList     *l;

	for (l = lrg_container_get_children (container); l != NULL; l = l->next)
	{
		lrg_widget_allocate (LRG_WIDGET (l->data), CRUMB_POPUP_PAD, y, w, row_h);
		y += row_h;
	}
}

static void
lrg_crumb_popup_paint (LrgWidget    *widget,
                       LrgPaintList *list)
{
	LrgPartLook surface;
	GList      *l;

	lrg_widget_resolve_look (widget, LRG_UI_PART_POPUP, &surface);
	lrg_widget_paint_box (widget, list, &surface, lrg_widget_get_world_x (widget),
	                      lrg_widget_get_world_y (widget), lrg_widget_get_width (widget),
	                      lrg_widget_get_height (widget));
	for (l = lrg_container_get_children (LRG_CONTAINER (widget)); l != NULL; l = l->next)
		lrg_widget_paint (LRG_WIDGET (l->data), list);
}

/* popup_focus_row: focus row @i with a visible highlight */
static void
popup_focus_row (LrgCrumbPopup *self,
                 gint           i,
                 gboolean       visible)
{
	LrgCanvas *canvas = find_canvas (LRG_WIDGET (self));
	guint      n = lrg_container_get_child_count (LRG_CONTAINER (self));
	LrgWidget *row;

	if (canvas == NULL || n == 0)
		return;
	row = lrg_container_get_child (LRG_CONTAINER (self), (guint) CLAMP (i, 0, (gint) n - 1));
	lrg_canvas_set_focused_widget (canvas, row);
	if (visible)
		lrg_widget_set_state_flags (row, LRG_WIDGET_STATE_FOCUS_VISIBLE);
}

static gboolean
lrg_crumb_popup_handle_event (LrgWidget        *widget,
                              const LrgUIEvent *event)
{
	LrgCrumbPopup *self = LRG_CRUMB_POPUP (widget);
	LrgCanvas     *canvas = find_canvas (widget);
	LrgWidget     *focused = canvas != NULL ? lrg_canvas_get_focused_widget (canvas) : NULL;
	gint           current = -1;
	gint           n = (gint) lrg_container_get_child_count (LRG_CONTAINER (self));
	GList         *l;
	gint           i;

	for (l = lrg_container_get_children (LRG_CONTAINER (self)), i = 0; l != NULL; l = l->next, i++)
		if (l->data == (gpointer) focused)
			current = i;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_UP:
			popup_focus_row (self, current < 0 ? n - 1 : current - 1, TRUE);
			return TRUE;
		case GRL_KEY_DOWN:
			popup_focus_row (self, current + 1, TRUE);
			return TRUE;
		case GRL_KEY_HOME:
			popup_focus_row (self, 0, TRUE);
			return TRUE;
		case GRL_KEY_END:
			popup_focus_row (self, n - 1, TRUE);
			return TRUE;
		default:
			return FALSE;
		}

	case LRG_UI_EVENT_CANCEL:
		if (self->crumb == NULL)
			return FALSE;
		{
			LrgBreadcrumb *crumb = self->crumb;

			lrg_breadcrumb_popdown (crumb);
			if (canvas != NULL && lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (crumb))
				lrg_widget_set_state_flags (LRG_WIDGET (crumb), LRG_WIDGET_STATE_FOCUS_VISIBLE);
		}
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		return lrg_ui_event_get_bubbling (event) ? FALSE : TRUE;

	default:
		return FALSE;
	}
}

static void
lrg_crumb_popup_class_init (LrgCrumbPopupClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_crumb_popup_dispose;
	widget_class->style_defaults = lrg_crumb_popup_style_defaults;
	widget_class->measure = lrg_crumb_popup_measure;
	widget_class->paint = lrg_crumb_popup_paint;
	widget_class->handle_event = lrg_crumb_popup_handle_event;
	container_class->layout_children = lrg_crumb_popup_layout_children;
}

static void
lrg_crumb_popup_init (LrgCrumbPopup *self)
{
}

/* ==========================================================================
 * Popup open / close
 * ========================================================================== */

static void
forget_popup (LrgBreadcrumb *self)
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
	LrgBreadcrumb *self = LRG_BREADCRUMB (user_data);

	/* Closed by a press on the ellipsis itself (the canvas closes
	 * popups before delivering the press): do not reopen it */
	if (self->popup != NULL && popup == LRG_WIDGET (self->popup))
		forget_popup (self);
}

/*
 * on_state_changed:
 *
 * The canvas closes non-modal popups on any press outside them before
 * delivering the press; when the list closes (OPEN cleared) while the
 * pointer is on the breadcrumb, that press must not reopen it.
 */
static void
on_state_changed (LrgWidget      *widget,
                  LrgWidgetState  previous,
                  gpointer        user_data)
{
	LrgBreadcrumb  *self = LRG_BREADCRUMB (widget);
	LrgWidgetState  now = lrg_widget_get_state_flags (widget);

	if ((previous & LRG_WIDGET_STATE_OPEN) && !(now & LRG_WIDGET_STATE_OPEN) &&
	    (now & LRG_WIDGET_STATE_HOVERED) && !self->closing)
		self->swallow_press = TRUE;
}

/*
 * open_popup:
 *
 * Shows the hidden segments in a list under the ellipsis, first hidden
 * segment on top, and focuses the first row (ring when @keyboard).
 */
static void
open_popup (LrgBreadcrumb *self,
            gboolean       keyboard)
{
	LrgCanvas     *canvas;
	LrgCrumbPopup *popup;
	Slot          *slot;
	const GQuark  *classes;
	guint          n_classes;
	guint          c;
	guint          i;

	if (self->popup != NULL || self->hidden_count == 0)
		return;
	canvas = find_canvas (LRG_WIDGET (self));
	slot = find_slot (self, KEY_ELLIPSIS);
	if (canvas == NULL || slot == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN_UI, "breadcrumb is not on a canvas; cannot open its list");
		return;
	}

	popup = g_object_new (LRG_TYPE_CRUMB_POPUP, NULL);
	g_set_weak_pointer (&popup->crumb, self);
	lrg_widget_set_theme (LRG_WIDGET (popup), lrg_widget_get_theme (LRG_WIDGET (self)));
	lrg_widget_set_appear (LRG_WIDGET (popup), LRG_APPEAR_FLY_DOWN, 0.0f);

	classes = lrg_widget_get_classes (LRG_WIDGET (self), &n_classes);
	for (c = 0; c < n_classes; c++)
		lrg_widget_add_class (LRG_WIDGET (popup), g_quark_to_string (classes[c]));

	for (i = self->hidden_first; i < self->hidden_first + self->hidden_count; i++)
	{
		LrgCrumbRow *row = g_object_new (LRG_TYPE_CRUMB_ROW, NULL);

		row->index = i;
		for (c = 0; c < n_classes; c++)
			lrg_widget_add_class (LRG_WIDGET (row), g_quark_to_string (classes[c]));
		lrg_container_add_child (LRG_CONTAINER (popup), LRG_WIDGET (row));
		g_object_unref (row);
	}

	self->popup = popup;
	g_set_weak_pointer (&self->canvas, canvas);
	self->dismissed_id = g_signal_connect_object (canvas, "popup-dismissed",
	                                              G_CALLBACK (on_popup_dismissed), self, 0);

	/* Anchor on the breadcrumb, then move under the ellipsis slot */
	lrg_canvas_show_popup (canvas, LRG_WIDGET (popup), LRG_WIDGET (self), LRG_POPUP_PLACEMENT_BELOW, FALSE);
	lrg_widget_set_x (LRG_WIDGET (popup), lrg_widget_get_x (LRG_WIDGET (popup)) + slot_left (self, slot));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POPUP_OPEN]);

	popup_focus_row (popup, 0, keyboard);
}

/* activate_key: the user picked slot @key */
static void
activate_key (LrgBreadcrumb *self,
              gint           key,
              gboolean       keyboard)
{
	if (key == KEY_ELLIPSIS)
	{
		if (self->popup != NULL)
			lrg_breadcrumb_popdown (self);
		else
			open_popup (self, keyboard);
		return;
	}
	if (key >= 0 && (guint) key < self->segments->len)
		g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0, (guint) key);
}

static void
crumb_activate_from_row (LrgBreadcrumb *self,
                         guint          index,
                         gboolean       keyboard)
{
	g_object_ref (self);

	lrg_breadcrumb_popdown (self);
	if (keyboard)
	{
		LrgCanvas *canvas = find_canvas (LRG_WIDGET (self));

		if (canvas != NULL && lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (self))
			lrg_widget_set_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_FOCUS_VISIBLE);
	}
	g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0, index);

	g_object_unref (self);
}

/* ==========================================================================
 * LrgWidget vfuncs
 * ========================================================================== */

static void
lrg_breadcrumb_style_defaults (LrgWidget      *widget,
                               LrgUIPart       part,
                               LrgWidgetState  state,
                               LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_ITEM:
		{
			const GrlColor *hover = lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
			GrlColor        rest = transparent (hover);

			/* Flat until hovered; the current (last) segment is SELECTED
			 * and reads in full TEXT, the ancestors are muted */
			if (state & LRG_WIDGET_STATE_PRESSED)
				lrg_part_look_set_background (look, lrg_theme_get_color (theme,
				                                                         LRG_THEME_COLOR_CONTROL_PRESSED));
			else if (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_OPEN))
				lrg_part_look_set_background (look, hover);
			else
				lrg_part_look_set_background (look, &rest);

			lrg_part_look_set_foreground (look,
			                              lrg_theme_get_color (theme,
			                                                   (state & (LRG_WIDGET_STATE_SELECTED |
			                                                             LRG_WIDGET_STATE_HOVERED |
			                                                             LRG_WIDGET_STATE_PRESSED))
			                                                   ? LRG_THEME_COLOR_TEXT
			                                                   : LRG_THEME_COLOR_TEXT_MUTED));
			lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));
			if (state & LRG_WIDGET_STATE_DISABLED)
				lrg_part_look_set_opacity (look, 0.45f);
		}
		break;

	case LRG_UI_PART_ARROW:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_size (look, roundf (lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE) * 0.3f));
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		break;

	case LRG_UI_PART_INDICATOR:
		{
			gfloat ring = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FOCUS_RING_WIDTH);

			/* Ring around the keyboard segment cursor */
			lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS), ring);
			lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS) + 2.0f);
		}
		break;

	case LRG_UI_PART_FOCUS:
		{
			GrlColor none = transparent (lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS));

			/* The segment cursor (INDICATOR) replaces the widget ring */
			lrg_part_look_set_border (look, &none, 0.0f);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_breadcrumb_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_breadcrumb_measure (LrgWidget *widget,
                        gfloat    *width,
                        gfloat    *height)
{
	LrgBreadcrumb *self = LRG_BREADCRUMB (widget);
	gfloat         w = 0.0f;
	guint          i;

	/* Natural width: every segment shown */
	for (i = 0; i < self->segments->len; i++)
		w += segment_width (self, i) + (i > 0 ? separator_width (widget) : 0.0f);

	*width = w;
	*height = lrg_theme_get_metric (lrg_widget_get_theme (widget), LRG_THEME_METRIC_CONTROL_HEIGHT);
}

static void
lrg_breadcrumb_arrange (LrgWidget *widget)
{
	relayout (LRG_BREADCRUMB (widget));
}

static gboolean
lrg_breadcrumb_step (LrgWidget *widget,
                     gfloat     delta_time)
{
	LrgBreadcrumb *self = LRG_BREADCRUMB (widget);
	LrgMotion      motion = lrg_widget_get_motion (widget);
	gboolean       busy = FALSE;
	gint           k;

	for (k = KEY_ELLIPSIS; k < (gint) self->segments->len; k++)
	{
		LrgSpring *s = key_spring (self, k);

		lrg_spring_set_motion (s, motion);
		lrg_spring_set_target (s, key_highlighted (self, k) ? 1.0f : 0.0f);
		if (!self->stepped || motion == LRG_MOTION_NONE)
			lrg_spring_snap (s, s->target);
		else
			busy |= lrg_spring_step (s, delta_time);
	}
	self->stepped = TRUE;
	return busy;
}

/* paint_chevron: a small ">" centred on (cx, cy), "<" when @rtl */
static void
paint_chevron (LrgPaintList      *list,
               const LrgPartLook *look,
               gfloat             cx,
               gfloat             cy,
               gboolean           rtl)
{
	gfloat s = look->size > 0.0f ? look->size : 4.0f;
	gfloat t = MAX (1.25f, s * 0.35f);
	gfloat d = rtl ? -1.0f : 1.0f;

	lrg_paint_list_line (list, cx - d * s * 0.4f, cy - s, cx + d * s * 0.45f, cy, t, &look->foreground);
	lrg_paint_list_line (list, cx + d * s * 0.45f, cy, cx - d * s * 0.4f, cy + s, t, &look->foreground);
}

static void
lrg_breadcrumb_paint (LrgWidget    *widget,
                      LrgPaintList *list)
{
	LrgBreadcrumb  *self = LRG_BREADCRUMB (widget);
	LrgWidgetState  base = lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_DISABLED;
	LrgPartLook     arrow;
	gfloat          x = lrg_widget_get_world_x (widget);
	gfloat          y = lrg_widget_get_world_y (widget);
	gfloat          w = lrg_widget_get_width (widget);
	gfloat          h = lrg_widget_get_height (widget);
	gfloat          cy = y + h * 0.5f;
	gfloat          pad = item_pad (widget);
	gfloat          inset = roundf (h * 0.12f);
	gboolean        rtl = lrg_widget_is_rtl (widget);
	guint           i;

	ensure_layout (self);
	lrg_widget_resolve_look (widget, LRG_UI_PART_ARROW, &arrow);

	lrg_paint_list_push_clip (list, x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f);
	for (i = 0; i < self->slots->len; i++)
	{
		Slot           *slot = &g_array_index (self->slots, Slot, i);
		LrgWidgetState  state = base;
		LrgPartLook     rest;
		LrgPartLook     lit;
		LrgPartLook     look;
		LrgSpring      *spring = key_spring (self, slot->key);
		gfloat          t;
		const gchar    *text;
		gfloat          sx = x + slot_left (self, slot);

		/* Chevron before every slot but the first (mirrored in RTL) */
		if (i > 0)
		{
			Slot   *prev = &g_array_index (self->slots, Slot, i - 1);
			gfloat  mid = (prev->x + prev->width + slot->x) * 0.5f;

			paint_chevron (list, &arrow, x + (rtl ? w - mid : mid), cy, rtl);
		}

		/* Per-slot state: current segment, open list, pressed */
		if (slot->key >= 0 && (guint) slot->key == self->segments->len - 1)
			state |= LRG_WIDGET_STATE_SELECTED;
		if (slot->key == KEY_ELLIPSIS && self->popup != NULL)
			state |= LRG_WIDGET_STATE_OPEN;

		/* Resting and highlighted looks blended by the slot's spring */
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, state, &rest);
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, state | LRG_WIDGET_STATE_HOVERED, &lit);
		t = spring != NULL ? (self->stepped ? spring->value : (key_highlighted (self, slot->key) ? 1.0f : 0.0f))
		                   : 0.0f;
		lrg_part_look_lerp (&rest, &lit, CLAMP (t, 0.0f, 1.0f), &look);
		if (slot->key == self->press_key && (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_PRESSED))
			lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM,
			                                   state | LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED,
			                                   &look);

		lrg_widget_paint_box (widget, list, &look, sx, y + inset, slot->width, h - inset * 2.0f);

		if (slot->key == KEY_ELLIPSIS)
			text = CRUMB_ELLIPSIS_TEXT;
		else
			text = slot->display != NULL ? slot->display : segment_at (self, (guint) slot->key)->label;

		lrg_paint_list_push_clip (list, sx, y, slot->width, h);
		paint_text_centered_v (list, widget, text, sx + pad, cy, &look.foreground);
		lrg_paint_list_pop_clip (list);

		/* Keyboard cursor */
		if (slot->key == self->focus_key &&
		    (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_FOCUS_VISIBLE))
		{
			LrgPartLook ring;

			lrg_widget_resolve_look (widget, LRG_UI_PART_INDICATOR, &ring);
			lrg_paint_list_rect_ex (list, sx - 1.0f, y + inset - 1.0f, slot->width + 2.0f,
			                        h - inset * 2.0f + 2.0f, &ring.radii, NULL, NULL, ring.border_width,
			                        &ring.border);
		}
	}
	lrg_paint_list_pop_clip (list);
}

/* move_focus_key: step the keyboard cursor by @delta slots */
static gboolean
move_focus_key (LrgBreadcrumb *self,
                gint           delta)
{
	gint pos = slot_index_of (self, self->focus_key);
	gint next;

	if (self->slots->len == 0)
		return FALSE;
	if (pos < 0)
		pos = (gint) self->slots->len - 1;
	next = pos + delta;
	if (next < 0 || next >= (gint) self->slots->len)
		return FALSE;
	self->focus_key = g_array_index (self->slots, Slot, next).key;
	return TRUE;
}

static gboolean
lrg_breadcrumb_handle_event (LrgWidget        *widget,
                             const LrgUIEvent *event)
{
	LrgBreadcrumb *self = LRG_BREADCRUMB (widget);
	gfloat         ex = lrg_ui_event_get_x (event);
	gfloat         ey = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_MOVE:
		self->hover_key = lrg_widget_contains_point (widget, ex, ey) ? key_at (self, ex) : KEY_NONE;
		return FALSE;

	case LRG_UI_EVENT_POINTER_ENTER:
		self->swallow_press = FALSE;
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		self->hover_key = KEY_NONE;
		return FALSE;

	case LRG_UI_EVENT_FOCUS_IN:
		if (slot_index_of (self, self->focus_key) < 0 && self->slots->len > 0)
			self->focus_key = g_array_index (self->slots, Slot, self->slots->len - 1).key;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		self->press_key = key_at (self, ex);
		if (self->press_key != KEY_NONE)
			self->focus_key = self->press_key;
		/* The ellipsis opens on press, like a combo box */
		if (self->press_key == KEY_ELLIPSIS && !self->swallow_press)
			activate_key (self, KEY_ELLIPSIS, FALSE);
		if (self->press_key == KEY_ELLIPSIS)
			self->press_key = KEY_NONE;
		self->swallow_press = FALSE;
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (self->press_key >= 0 && lrg_widget_contains_point (widget, ex, ey) &&
		    key_at (self, ex) == self->press_key)
		{
			gint key = self->press_key;

			self->press_key = KEY_NONE;
			activate_key (self, key, FALSE);
		}
		self->press_key = KEY_NONE;
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		switch (lrg_ui_event_get_key (event))
		{
		/* Arrows follow the trail, which is mirrored in RTL */
		case GRL_KEY_LEFT:
			return move_focus_key (self, lrg_widget_is_rtl (widget) ? 1 : -1);
		case GRL_KEY_RIGHT:
			return move_focus_key (self, lrg_widget_is_rtl (widget) ? -1 : 1);
		case GRL_KEY_HOME:
			if (self->slots->len > 0)
				self->focus_key = g_array_index (self->slots, Slot, 0).key;
			return TRUE;
		case GRL_KEY_END:
			if (self->slots->len > 0)
				self->focus_key = g_array_index (self->slots, Slot, self->slots->len - 1).key;
			return TRUE;
		default:
			return FALSE;
		}

	case LRG_UI_EVENT_ACTIVATE:
		if (slot_index_of (self, self->focus_key) < 0 && self->slots->len > 0)
			self->focus_key = g_array_index (self->slots, Slot, self->slots->len - 1).key;
		activate_key (self, self->focus_key, TRUE);
		return TRUE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_breadcrumb_dispose (GObject *object)
{
	LrgBreadcrumb *self = LRG_BREADCRUMB (object);

	if (self->popup != NULL && self->canvas != NULL)
	{
		g_autoptr(LrgCanvas) canvas = g_object_ref (self->canvas);
		g_autoptr(LrgWidget) popup = LRG_WIDGET (g_object_ref (self->popup));

		forget_popup (self);
		lrg_canvas_dismiss_popup (canvas, popup);
	}
	forget_popup (self);

	G_OBJECT_CLASS (lrg_breadcrumb_parent_class)->dispose (object);
}

static void
lrg_breadcrumb_finalize (GObject *object)
{
	LrgBreadcrumb *self = LRG_BREADCRUMB (object);

	g_clear_pointer (&self->segments, g_array_unref);
	g_clear_pointer (&self->slots, g_array_unref);

	G_OBJECT_CLASS (lrg_breadcrumb_parent_class)->finalize (object);
}

static void
lrg_breadcrumb_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgBreadcrumb *self = LRG_BREADCRUMB (object);

	switch (prop_id)
	{
	case PROP_N_SEGMENTS:
		g_value_set_uint (value, self->segments->len);
		break;
	case PROP_COLLAPSED:
		g_value_set_boolean (value, lrg_breadcrumb_get_collapsed (self));
		break;
	case PROP_POPUP_OPEN:
		g_value_set_boolean (value, self->popup != NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_breadcrumb_class_init (LrgBreadcrumbClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_breadcrumb_dispose;
	object_class->finalize = lrg_breadcrumb_finalize;
	object_class->get_property = lrg_breadcrumb_get_property;

	widget_class->style_defaults = lrg_breadcrumb_style_defaults;
	widget_class->measure = lrg_breadcrumb_measure;
	widget_class->arrange = lrg_breadcrumb_arrange;
	widget_class->paint = lrg_breadcrumb_paint;
	widget_class->step = lrg_breadcrumb_step;
	widget_class->handle_event = lrg_breadcrumb_handle_event;

	/**
	 * LrgBreadcrumb:n-segments:
	 *
	 * Number of segments.
	 */
	properties[PROP_N_SEGMENTS] =
		g_param_spec_uint ("n-segments", NULL, NULL, 0, G_MAXUINT, 0,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBreadcrumb:collapsed:
	 *
	 * Whether middle segments are folded into the ellipsis at the
	 * current width.
	 */
	properties[PROP_COLLAPSED] =
		g_param_spec_boolean ("collapsed", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBreadcrumb:popup-open:
	 *
	 * Whether the list of hidden segments is showing.
	 */
	properties[PROP_POPUP_OPEN] =
		g_param_spec_boolean ("popup-open", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgBreadcrumb::activated:
	 * @self: the breadcrumb
	 * @index: the segment that was clicked or activated
	 *
	 * Emitted when a segment is clicked, activated from the keyboard
	 * or gamepad, or picked from the list of hidden segments.
	 */
	signals[SIGNAL_ACTIVATED] =
		g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
lrg_breadcrumb_init (LrgBreadcrumb *self)
{
	self->segments = g_array_new (FALSE, TRUE, sizeof (Segment));
	g_array_set_clear_func (self->segments, segment_clear);
	self->slots = g_array_new (FALSE, TRUE, sizeof (Slot));
	g_array_set_clear_func (self->slots, slot_clear);
	self->laid_width = -1.0f;
	self->hover_key = KEY_NONE;
	self->press_key = KEY_NONE;
	self->focus_key = KEY_NONE;
	lrg_spring_init (&self->ellipsis_hover, 0.0f, LRG_MOTION_SMOOTH);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
	g_signal_connect (self, "state-changed", G_CALLBACK (on_state_changed), NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgBreadcrumb *
lrg_breadcrumb_new (void)
{
	return g_object_new (LRG_TYPE_BREADCRUMB, NULL);
}

guint
lrg_breadcrumb_append (LrgBreadcrumb *self,
                       const gchar   *label)
{
	return lrg_breadcrumb_append_with_data (self, label, NULL, NULL);
}

guint
lrg_breadcrumb_append_with_data (LrgBreadcrumb  *self,
                                 const gchar    *label,
                                 gpointer        data,
                                 GDestroyNotify  destroy)
{
	Segment s;

	g_return_val_if_fail (LRG_IS_BREADCRUMB (self), 0);
	g_return_val_if_fail (label != NULL, 0);

	lrg_breadcrumb_popdown (self);

	memset (&s, 0, sizeof (Segment));
	s.label = g_strdup (label);
	s.data = data;
	s.destroy = destroy;
	lrg_spring_init (&s.hover, 0.0f, LRG_MOTION_SMOOTH);
	g_array_append_val (self->segments, s);

	invalidate (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_SEGMENTS]);
	return self->segments->len - 1;
}

void
lrg_breadcrumb_truncate (LrgBreadcrumb *self,
                         guint          n_segments)
{
	g_return_if_fail (LRG_IS_BREADCRUMB (self));

	if (n_segments >= self->segments->len)
		return;

	lrg_breadcrumb_popdown (self);
	g_array_set_size (self->segments, n_segments);
	if (self->focus_key >= (gint) n_segments)
		self->focus_key = n_segments > 0 ? (gint) n_segments - 1 : KEY_NONE;
	self->hover_key = KEY_NONE;
	self->press_key = KEY_NONE;
	invalidate (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_SEGMENTS]);
}

void
lrg_breadcrumb_remove_all (LrgBreadcrumb *self)
{
	lrg_breadcrumb_truncate (self, 0);
}

guint
lrg_breadcrumb_get_n_segments (LrgBreadcrumb *self)
{
	g_return_val_if_fail (LRG_IS_BREADCRUMB (self), 0);

	return self->segments->len;
}

const gchar *
lrg_breadcrumb_get_segment_label (LrgBreadcrumb *self,
                                  guint          index)
{
	g_return_val_if_fail (LRG_IS_BREADCRUMB (self), NULL);

	if (index >= self->segments->len)
		return NULL;
	return segment_at (self, index)->label;
}

gpointer
lrg_breadcrumb_get_segment_data (LrgBreadcrumb *self,
                                 guint          index)
{
	g_return_val_if_fail (LRG_IS_BREADCRUMB (self), NULL);

	if (index >= self->segments->len)
		return NULL;
	return segment_at (self, index)->data;
}

gboolean
lrg_breadcrumb_get_collapsed (LrgBreadcrumb *self)
{
	g_return_val_if_fail (LRG_IS_BREADCRUMB (self), FALSE);

	ensure_layout (self);
	return self->hidden_count > 0;
}

gboolean
lrg_breadcrumb_get_hidden_range (LrgBreadcrumb *self,
                                 guint         *first,
                                 guint         *n_hidden)
{
	g_return_val_if_fail (LRG_IS_BREADCRUMB (self), FALSE);

	ensure_layout (self);
	if (first != NULL)
		*first = self->hidden_first;
	if (n_hidden != NULL)
		*n_hidden = self->hidden_count;
	return self->hidden_count > 0;
}

gboolean
lrg_breadcrumb_get_segment_bounds (LrgBreadcrumb *self,
                                   gint           index,
                                   GrlRectangle  *bounds)
{
	Slot *slot;

	g_return_val_if_fail (LRG_IS_BREADCRUMB (self), FALSE);
	g_return_val_if_fail (bounds != NULL, FALSE);

	slot = find_slot (self, index);
	if (slot == NULL)
		return FALSE;
	bounds->x = lrg_widget_get_world_x (LRG_WIDGET (self)) + slot_left (self, slot);
	bounds->y = lrg_widget_get_world_y (LRG_WIDGET (self));
	bounds->width = slot->width;
	bounds->height = lrg_widget_get_height (LRG_WIDGET (self));
	return TRUE;
}

gboolean
lrg_breadcrumb_get_popup_open (LrgBreadcrumb *self)
{
	g_return_val_if_fail (LRG_IS_BREADCRUMB (self), FALSE);

	return self->popup != NULL;
}

void
lrg_breadcrumb_popdown (LrgBreadcrumb *self)
{
	g_return_if_fail (LRG_IS_BREADCRUMB (self));

	if (self->popup == NULL)
		return;
	self->closing = TRUE;
	if (self->canvas != NULL)
	{
		g_autoptr(LrgCanvas) canvas = g_object_ref (self->canvas);
		g_autoptr(LrgWidget) popup = LRG_WIDGET (g_object_ref (self->popup));

		lrg_canvas_dismiss_popup (canvas, popup);
	}
	forget_popup (self);
	self->closing = FALSE;
}
