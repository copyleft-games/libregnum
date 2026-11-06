/* lrg-combo-box.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgComboBox: A button that opens a popup list to pick one of several items.
 *
 * The combo box itself paints like a button (BOX part) with the
 * selected text and a chevron (ARROW part) that flips while the list is
 * open. The list is a private popup widget (LrgComboPopup) shown on the
 * canvas popup layer, holding one focusable row widget (LrgComboRow)
 * per item so the canvas focus machinery (Tab, spatial navigation,
 * gamepad) reaches every row.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-combo-box.h"
#include "lrg-container.h"
#include "lrg-canvas.h"
#include "../tween/lrg-spring.h"
#include "../lrg-log.h"

/* Rows shown before the list scrolls */
#define COMBO_MAX_VISIBLE_ROWS (8)

/* Inset between the popup edge and its rows */
#define COMBO_POPUP_PAD (4.0f)

/* Width reserved for the scroll indicator when the list scrolls */
#define COMBO_SCROLL_GUTTER (6.0f)

/* ==========================================================================
 * Private types: the popup list and its rows
 * ========================================================================== */

#define LRG_TYPE_COMBO_POPUP (lrg_combo_popup_get_type ())
G_DECLARE_FINAL_TYPE (LrgComboPopup, lrg_combo_popup, LRG, COMBO_POPUP, LrgContainer)

#define LRG_TYPE_COMBO_ROW (lrg_combo_row_get_type ())
G_DECLARE_FINAL_TYPE (LrgComboRow, lrg_combo_row, LRG, COMBO_ROW, LrgWidget)

struct _LrgComboBox
{
	LrgWidget      parent_instance;

	GPtrArray     *items;          /* gchar *, owned */
	gint           selected;       /* -1 = none */
	gchar         *placeholder;

	/* Open list */
	LrgComboPopup *popup;          /* owned while open */
	LrgCanvas     *canvas;         /* weak: the canvas showing the popup */
	gulong         dismissed_id;

	/* Chevron flip, 0 = closed (pointing down), 1 = open (up) */
	LrgSpring      arrow;
	gboolean       stepped;        /* FALSE: show the state, no flip */

	/* A press that closed the list must not reopen it */
	gboolean       swallow_press;
};

struct _LrgComboPopup
{
	LrgContainer  parent_instance;

	LrgComboBox  *combo;           /* weak */
	guint         first;           /* first row in view */
	guint         n_view;          /* rows that fit in view */
};

struct _LrgComboRow
{
	LrgWidget  parent_instance;

	guint      index;
};

G_DEFINE_FINAL_TYPE (LrgComboBox, lrg_combo_box, LRG_TYPE_WIDGET)
G_DEFINE_FINAL_TYPE (LrgComboPopup, lrg_combo_popup, LRG_TYPE_CONTAINER)
G_DEFINE_FINAL_TYPE (LrgComboRow, lrg_combo_row, LRG_TYPE_WIDGET)

enum
{
	PROP_0,
	PROP_SELECTED,
	PROP_PLACEHOLDER,
	PROP_N_ITEMS,
	PROP_POPUP_OPEN,
	N_PROPS
};

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void combo_select_from_row (LrgComboBox *self,
                                   guint        index,
                                   gboolean     keyboard);

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* find_canvas: the canvas @widget lives in, or NULL */
static LrgCanvas *
find_canvas (LrgWidget *widget)
{
	LrgWidget *root = lrg_widget_get_root (widget);

	return LRG_IS_CANVAS (root) ? LRG_CANVAS (root) : NULL;
}

/* transparent: @color with zero alpha (blends only the alpha in) */
static GrlColor
transparent (const GrlColor *color)
{
	GrlColor c = *color;

	c.a = 0;
	return c;
}

/* text_width: width of @text in the theme's UI font at @size */
static gfloat
text_width (LrgTheme    *theme,
            const gchar *text,
            gfloat       size)
{
	gfloat w = 0.0f;

	if (text == NULL || text[0] == '\0')
		return 0.0f;
	lrg_paint_measure_text (lrg_theme_get_ui_font (theme), text, size, 1.0f, &w, NULL);
	return w;
}

/*
 * paint_text_centered_v:
 *
 * Draws @text with its left edge at @x, centred vertically on @cy.
 */
static void
paint_text_centered_v (LrgPaintList   *list,
                       LrgTheme       *theme,
                       const gchar    *text,
                       gfloat          x,
                       gfloat          cy,
                       gfloat          size,
                       const GrlColor *color)
{
	GrlFont *font = lrg_theme_get_ui_font (theme);
	gfloat   th = size;

	if (text == NULL || text[0] == '\0')
		return;
	lrg_paint_measure_text (font, text, size, 1.0f, NULL, &th);
	lrg_paint_list_text (list, font, text, roundf (x), roundf (cy - th * 0.5f), size, 1.0f, color);
}

/* ==========================================================================
 * LrgComboRow: one focusable item row in the popup
 * ========================================================================== */

/* row_popup: the popup holding @row */
static LrgComboPopup *
row_popup (LrgComboRow *row)
{
	LrgContainer *parent = lrg_widget_get_parent (LRG_WIDGET (row));

	return LRG_IS_COMBO_POPUP (parent) ? LRG_COMBO_POPUP (parent) : NULL;
}

/* row_combo: the combo box the row belongs to, or NULL once it is gone */
static LrgComboBox *
row_combo (LrgComboRow *row)
{
	LrgComboPopup *popup = row_popup (row);

	return popup != NULL ? popup->combo : NULL;
}

/* row_in_view: the row is inside the popup's scrolled viewport */
static gboolean
row_in_view (LrgComboRow *row)
{
	LrgComboPopup *popup = row_popup (row);

	if (popup == NULL)
		return FALSE;
	return row->index >= popup->first && row->index < popup->first + popup->n_view;
}

static void
lrg_combo_row_style_defaults (LrgWidget      *widget,
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

			/* Rows are flat until hovered, pressed or keyboard-focused */
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

	case LRG_UI_PART_MARK:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		break;

	case LRG_UI_PART_FOCUS:
		{
			GrlColor none = transparent (lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS));

			/* The highlighted row is the focus indicator; no ring */
			lrg_part_look_set_border (look, &none, 0.0f);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_combo_row_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_combo_row_measure (LrgWidget *widget,
                       gfloat    *width,
                       gfloat    *height)
{
	LrgComboRow *self = LRG_COMBO_ROW (widget);
	LrgComboBox *combo = row_combo (self);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	const gchar *text = combo != NULL ? lrg_combo_box_get_item_text (combo, self->index) : NULL;

	/* padding | check gutter | text | padding */
	*width = pad * 2.0f + size + text_width (theme, text, size);
	*height = lrg_theme_get_metric (theme, LRG_THEME_METRIC_ROW_HEIGHT);
}

static void
lrg_combo_row_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	LrgComboRow *self = LRG_COMBO_ROW (widget);
	LrgComboBox *combo = row_combo (self);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	LrgPartLook  item;
	gfloat       x = lrg_widget_get_world_x (widget);
	gfloat       y = lrg_widget_get_world_y (widget);
	gfloat       w = lrg_widget_get_width (widget);
	gfloat       h = lrg_widget_get_height (widget);
	gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	gfloat       cy = y + h * 0.5f;

	if (combo == NULL)
		return;

	/* Highlight */
	lrg_widget_resolve_look (widget, LRG_UI_PART_ITEM, &item);
	lrg_widget_paint_box (widget, list, &item, x, y, w, h);

	/* Check mark on the selected item: a short and a long stroke */
	if ((gint) self->index == combo->selected)
	{
		LrgPartLook mark;
		gfloat      s = size * 0.5f;
		gfloat      mx = x + pad + size * 0.5f;
		gfloat      t = MAX (1.5f, size * 0.12f);

		lrg_widget_resolve_look (widget, LRG_UI_PART_MARK, &mark);
		lrg_paint_list_line (list, mx - s * 0.55f, cy, mx - s * 0.15f, cy + s * 0.4f, t, &mark.foreground);
		lrg_paint_list_line (list, mx - s * 0.15f, cy + s * 0.4f, mx + s * 0.6f, cy - s * 0.45f, t,
		                     &mark.foreground);
	}

	paint_text_centered_v (list, theme, lrg_combo_box_get_item_text (combo, self->index),
	                       x + pad + size, cy, size, &item.foreground);
}

static gboolean
lrg_combo_row_handle_event (LrgWidget        *widget,
                            const LrgUIEvent *event)
{
	LrgComboRow *self = LRG_COMBO_ROW (widget);
	LrgComboBox *combo = row_combo (self);
	gboolean     inside;

	if (combo == NULL)
		return FALSE;

	inside = lrg_widget_contains_point (widget, lrg_ui_event_get_x (event), lrg_ui_event_get_y (event));

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		return lrg_ui_event_get_button (event) == 0 && inside && row_in_view (self);

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		/* Pick on release inside a row that is actually in view (rows
		 * scrolled out can poke into the popup's edge padding) */
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (inside && row_in_view (self))
			combo_select_from_row (combo, self->index, FALSE);
		return TRUE;

	case LRG_UI_EVENT_ACTIVATE:
		combo_select_from_row (combo, self->index, TRUE);
		return TRUE;

	default:
		return FALSE;
	}
}

static void
lrg_combo_row_class_init (LrgComboRowClass *klass)
{
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	widget_class->style_defaults = lrg_combo_row_style_defaults;
	widget_class->measure = lrg_combo_row_measure;
	widget_class->paint = lrg_combo_row_paint;
	widget_class->handle_event = lrg_combo_row_handle_event;
}

static void
lrg_combo_row_init (LrgComboRow *self)
{
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * LrgComboPopup: the list surface
 * ========================================================================== */

static void
lrg_combo_popup_dispose (GObject *object)
{
	LrgComboPopup *self = LRG_COMBO_POPUP (object);

	g_clear_weak_pointer (&self->combo);

	G_OBJECT_CLASS (lrg_combo_popup_parent_class)->dispose (object);
}

/* popup_n_rows: number of rows (= items when built) */
static guint
popup_n_rows (LrgComboPopup *self)
{
	return lrg_container_get_child_count (LRG_CONTAINER (self));
}

/* popup_scrolls: more rows than fit in view */
static gboolean
popup_scrolls (LrgComboPopup *self)
{
	return popup_n_rows (self) > self->n_view;
}

static void
lrg_combo_popup_style_defaults (LrgWidget      *widget,
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

	case LRG_UI_PART_SCROLL_THUMB:
		{
			GrlColor c;

			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED), 0.45f, &c);
			lrg_part_look_set_background (look, &c);
			lrg_part_look_set_radius (look, G_MAXFLOAT);
		}
		break;

	default:
		LRG_WIDGET_CLASS (lrg_combo_popup_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_combo_popup_measure (LrgWidget *widget,
                         gfloat    *width,
                         gfloat    *height)
{
	LrgComboPopup *self = LRG_COMBO_POPUP (widget);
	LrgTheme      *theme = lrg_widget_get_theme (widget);
	gfloat         row_h = lrg_theme_get_metric (theme, LRG_THEME_METRIC_ROW_HEIGHT);
	guint          n = popup_n_rows (self);
	gfloat         w = 0.0f;
	GList         *l;

	/* Widest row, plus the edge inset and the scroll gutter */
	for (l = lrg_container_get_children (LRG_CONTAINER (self)); l != NULL; l = l->next)
	{
		gfloat rw;

		lrg_widget_get_preferred_size (LRG_WIDGET (l->data), &rw, NULL);
		w = MAX (w, rw);
	}
	w += COMBO_POPUP_PAD * 2.0f;
	if (n > COMBO_MAX_VISIBLE_ROWS)
		w += COMBO_SCROLL_GUTTER;

	*width = w;
	*height = COMBO_POPUP_PAD * 2.0f + row_h * (gfloat) MIN (n, COMBO_MAX_VISIBLE_ROWS);
}

/*
 * lrg_combo_popup_layout_children:
 *
 * Stacks the rows at ROW_HEIGHT pitch, shifted up by the scroll offset
 * (whole rows). Rows out of view keep their slots above / below the
 * viewport so spatial navigation still finds them; painting clips them.
 */
static void
lrg_combo_popup_layout_children (LrgContainer *container)
{
	LrgComboPopup *self = LRG_COMBO_POPUP (container);
	LrgWidget     *widget = LRG_WIDGET (container);
	LrgTheme      *theme = lrg_widget_get_theme (widget);
	gfloat         row_h = lrg_theme_get_metric (theme, LRG_THEME_METRIC_ROW_HEIGHT);
	gfloat         h = lrg_widget_get_height (widget);
	gfloat         row_w;
	guint          n = popup_n_rows (self);
	guint          i;
	GList         *l;

	/* How many rows fit, and keep the scroll offset in range */
	self->n_view = row_h > 0.0f ? (guint) MAX (1.0f, floorf ((h - COMBO_POPUP_PAD * 2.0f) / row_h + 0.01f)) : 1;
	self->n_view = MIN (self->n_view, MAX (n, 1u));
	if (self->first + self->n_view > n)
		self->first = n > self->n_view ? n - self->n_view : 0;

	row_w = lrg_widget_get_width (widget) - COMBO_POPUP_PAD * 2.0f -
	        (popup_scrolls (self) ? COMBO_SCROLL_GUTTER : 0.0f);

	for (l = lrg_container_get_children (container), i = 0; l != NULL; l = l->next, i++)
	{
		gfloat y = COMBO_POPUP_PAD + ((gfloat) i - (gfloat) self->first) * row_h;

		lrg_widget_allocate (LRG_WIDGET (l->data), COMBO_POPUP_PAD, y, MAX (row_w, 0.0f), row_h);
	}
}

/* popup_scroll_to: set the first row in view and re-place the rows */
static void
popup_scroll_to (LrgComboPopup *self,
                 gint           first)
{
	guint n = popup_n_rows (self);
	gint  max_first = n > self->n_view ? (gint) (n - self->n_view) : 0;

	self->first = (guint) CLAMP (first, 0, max_first);
	lrg_container_layout_children (LRG_CONTAINER (self));
}

/* popup_ensure_visible: scroll the least so row @index is in view */
static void
popup_ensure_visible (LrgComboPopup *self,
                      guint          index)
{
	if (index < self->first)
		popup_scroll_to (self, (gint) index);
	else if (self->n_view > 0 && index >= self->first + self->n_view)
		popup_scroll_to (self, (gint) (index - self->n_view + 1));
}

/* popup_focus_row: focus row @index (keyboard focus shows the highlight) */
static void
popup_focus_row (LrgComboPopup *self,
                 guint          index,
                 gboolean       visible)
{
	LrgCanvas *canvas = find_canvas (LRG_WIDGET (self));
	LrgWidget *row;

	if (index >= popup_n_rows (self))
		return;
	row = lrg_container_get_child (LRG_CONTAINER (self), index);
	popup_ensure_visible (self, index);
	if (canvas == NULL)
		return;
	lrg_canvas_set_focused_widget (canvas, row);
	if (visible)
		lrg_widget_set_state_flags (row, LRG_WIDGET_STATE_FOCUS_VISIBLE);
}

static void
lrg_combo_popup_paint (LrgWidget    *widget,
                       LrgPaintList *list)
{
	LrgComboPopup *self = LRG_COMBO_POPUP (widget);
	LrgPartLook    surface;
	gfloat         x = lrg_widget_get_world_x (widget);
	gfloat         y = lrg_widget_get_world_y (widget);
	gfloat         w = lrg_widget_get_width (widget);
	gfloat         h = lrg_widget_get_height (widget);
	GList         *l;
	guint          i;

	/* Surface: shadow, fill, hairline border */
	lrg_widget_resolve_look (widget, LRG_UI_PART_POPUP, &surface);
	lrg_widget_paint_box (widget, list, &surface, x, y, w, h);

	/* Rows, clipped to the viewport; only those that can show */
	lrg_paint_list_push_clip (list, x, y + COMBO_POPUP_PAD, w, h - COMBO_POPUP_PAD * 2.0f);
	for (l = lrg_container_get_children (LRG_CONTAINER (self)), i = 0; l != NULL; l = l->next, i++)
		if (i + 1 >= self->first && i <= self->first + self->n_view)
			lrg_widget_paint (LRG_WIDGET (l->data), list);
	lrg_paint_list_pop_clip (list);

	/* Scroll indicator: a slim pill in the gutter */
	if (popup_scrolls (self))
	{
		LrgPartLook thumb;
		guint       n = popup_n_rows (self);
		gfloat      track = h - COMBO_POPUP_PAD * 4.0f;
		gfloat      th = MAX (12.0f, track * (gfloat) self->n_view / (gfloat) n);
		gfloat      ty = y + COMBO_POPUP_PAD * 2.0f +
		                 (track - th) * (gfloat) self->first / (gfloat) (n - self->n_view);

		lrg_widget_resolve_look (widget, LRG_UI_PART_SCROLL_THUMB, &thumb);
		lrg_widget_paint_box (widget, list, &thumb, x + w - COMBO_POPUP_PAD - 3.0f - 1.0f, ty, 3.0f, th);
	}
}

/* focused_row_index: index of the event's row target, or -1 */
static gint
focused_row_index (LrgComboPopup    *self,
                   const LrgUIEvent *event)
{
	LrgWidget *target = lrg_ui_event_get_target (event);
	LrgCanvas *canvas;

	if (LRG_IS_COMBO_ROW (target))
		return (gint) LRG_COMBO_ROW (target)->index;

	canvas = find_canvas (LRG_WIDGET (self));
	target = canvas != NULL ? lrg_canvas_get_focused_widget (canvas) : NULL;
	if (target != NULL && LRG_IS_COMBO_ROW (target) &&
	    lrg_widget_get_parent (target) == LRG_CONTAINER (self))
		return (gint) LRG_COMBO_ROW (target)->index;
	return -1;
}

static gboolean
lrg_combo_popup_handle_event (LrgWidget        *widget,
                              const LrgUIEvent *event)
{
	LrgComboPopup *self = LRG_COMBO_POPUP (widget);
	gint           n = (gint) popup_n_rows (self);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_KEY_DOWN:
		{
			gint current = focused_row_index (self, event);
			gint next;
			gint page = MAX ((gint) self->n_view - 1, 1);

			/* Arrow keys walk the rows (and scroll) rather than
			 * letting spatial navigation leave the list */
			switch (lrg_ui_event_get_key (event))
			{
			case GRL_KEY_UP:
				next = current < 0 ? n - 1 : current - 1;
				break;
			case GRL_KEY_DOWN:
				next = current < 0 ? 0 : current + 1;
				break;
			case GRL_KEY_PAGE_UP:
				next = current - page;
				break;
			case GRL_KEY_PAGE_DOWN:
				next = current + page;
				break;
			case GRL_KEY_HOME:
				next = 0;
				break;
			case GRL_KEY_END:
				next = n - 1;
				break;
			default:
				return FALSE;
			}
			if (n > 0)
				popup_focus_row (self, (guint) CLAMP (next, 0, n - 1), TRUE);
			return TRUE;
		}

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat dy = lrg_ui_event_get_scroll_y (event);
			gint   steps;

			if (!popup_scrolls (self) || dy == 0.0f)
				return TRUE;
			/* Wheel up (positive) shows earlier rows */
			steps = (gint) MAX (1.0f, roundf (fabsf (dy)));
			popup_scroll_to (self, (gint) self->first + (dy > 0.0f ? -steps : steps));
			return TRUE;
		}

	case LRG_UI_EVENT_CANCEL:
		{
			LrgComboBox *combo = self->combo;
			LrgCanvas   *canvas = find_canvas (widget);

			/* Escape / B: close, focus back on the combo with its ring */
			if (combo == NULL)
				return FALSE;
			lrg_combo_box_popdown (combo);
			if (canvas != NULL && lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (combo))
				lrg_widget_set_state_flags (LRG_WIDGET (combo), LRG_WIDGET_STATE_FOCUS_VISIBLE);
			return TRUE;
		}

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		/* Clicks on the padding are swallowed, not passed to rows */
		return lrg_ui_event_get_bubbling (event) ? FALSE : TRUE;

	default:
		return FALSE;
	}
}

static void
lrg_combo_popup_class_init (LrgComboPopupClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->dispose = lrg_combo_popup_dispose;

	widget_class->style_defaults = lrg_combo_popup_style_defaults;
	widget_class->measure = lrg_combo_popup_measure;
	widget_class->paint = lrg_combo_popup_paint;
	widget_class->handle_event = lrg_combo_popup_handle_event;

	container_class->layout_children = lrg_combo_popup_layout_children;
}

static void
lrg_combo_popup_init (LrgComboPopup *self)
{
	self->n_view = 1;
}

/*
 * combo_popup_new:
 *
 * Builds the list for @combo: one row per item, carrying the combo's
 * theme and style classes so theme rules aimed at it apply here too.
 */
static LrgComboPopup *
combo_popup_new (LrgComboBox *combo)
{
	LrgComboPopup *popup = g_object_new (LRG_TYPE_COMBO_POPUP, NULL);
	const GQuark  *classes;
	guint          n_classes;
	guint          i;
	guint          c;

	g_set_weak_pointer (&popup->combo, combo);
	lrg_widget_set_theme (LRG_WIDGET (popup), lrg_widget_get_theme (LRG_WIDGET (combo)));
	lrg_widget_set_appear (LRG_WIDGET (popup), LRG_APPEAR_FLY_DOWN, 0.0f);

	classes = lrg_widget_get_classes (LRG_WIDGET (combo), &n_classes);
	for (c = 0; c < n_classes; c++)
		lrg_widget_add_class (LRG_WIDGET (popup), g_quark_to_string (classes[c]));

	for (i = 0; i < combo->items->len; i++)
	{
		LrgComboRow *row = g_object_new (LRG_TYPE_COMBO_ROW, NULL);

		row->index = i;
		for (c = 0; c < n_classes; c++)
			lrg_widget_add_class (LRG_WIDGET (row), g_quark_to_string (classes[c]));
		lrg_container_add_child (LRG_CONTAINER (popup), LRG_WIDGET (row));
		g_object_unref (row);
	}

	return popup;
}

/* ==========================================================================
 * LrgComboBox
 * ========================================================================== */

/* forget_popup: drop our reference to the list and its canvas hook */
static void
forget_popup (LrgComboBox *self)
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

/* on_popup_dismissed: the canvas closed a popup; was it ours? */
static void
on_popup_dismissed (LrgCanvas *canvas,
                    LrgWidget *popup,
                    gpointer   user_data)
{
	LrgComboBox *self = LRG_COMBO_BOX (user_data);

	if (self->popup != NULL && popup == LRG_WIDGET (self->popup))
		forget_popup (self);
}

/*
 * on_state_changed:
 *
 * When the list closes while the pointer is on the combo, the close was
 * a click on the combo (the canvas dismisses non-modal popups on any
 * press outside them, before delivering the press). Remember it so that
 * same press does not reopen the list. Entering the combo again (a new
 * hover) forgets it.
 */
static void
on_state_changed (LrgWidget      *widget,
                  LrgWidgetState  previous,
                  gpointer        user_data)
{
	LrgComboBox    *self = LRG_COMBO_BOX (widget);
	LrgWidgetState  now = lrg_widget_get_state_flags (widget);

	if ((previous & LRG_WIDGET_STATE_OPEN) && !(now & LRG_WIDGET_STATE_OPEN) &&
	    (now & LRG_WIDGET_STATE_HOVERED))
		self->swallow_press = TRUE;
}

/*
 * open_popup:
 *
 * Shows the list below the combo, at least as wide as it, and focuses
 * the selected row (with a visible ring when @keyboard).
 */
static void
open_popup (LrgComboBox *self,
            gboolean     keyboard)
{
	LrgCanvas *canvas;
	gfloat     pw;
	gfloat     ph;

	if (self->popup != NULL || self->items->len == 0)
		return;

	canvas = find_canvas (LRG_WIDGET (self));
	if (canvas == NULL)
	{
		lrg_debug (LRG_LOG_DOMAIN_UI, "combo box is not on a canvas; cannot open its list");
		return;
	}

	self->popup = combo_popup_new (self);
	lrg_widget_get_preferred_size (LRG_WIDGET (self->popup), &pw, &ph);
	lrg_widget_set_size (LRG_WIDGET (self->popup), MAX (pw, lrg_widget_get_width (LRG_WIDGET (self))), ph);

	g_set_weak_pointer (&self->canvas, canvas);
	self->dismissed_id = g_signal_connect_object (canvas, "popup-dismissed",
	                                              G_CALLBACK (on_popup_dismissed), self, 0);

	lrg_canvas_show_popup (canvas, LRG_WIDGET (self->popup), LRG_WIDGET (self),
	                       LRG_POPUP_PLACEMENT_BELOW, FALSE);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_POPUP_OPEN]);

	popup_focus_row (self->popup, self->selected >= 0 ? (guint) self->selected : 0, keyboard);
}

/*
 * combo_select_from_row:
 *
 * A row was picked: close the list first (focus returns to the combo),
 * then change the selection so "changed" handlers see a settled UI.
 */
static void
combo_select_from_row (LrgComboBox *self,
                       guint        index,
                       gboolean     keyboard)
{
	g_object_ref (self);

	lrg_combo_box_popdown (self);
	if (keyboard)
	{
		LrgCanvas *canvas = find_canvas (LRG_WIDGET (self));

		if (canvas != NULL && lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (self))
			lrg_widget_set_state_flags (LRG_WIDGET (self), LRG_WIDGET_STATE_FOCUS_VISIBLE);
	}
	lrg_combo_box_set_selected (self, (gint) index);

	g_object_unref (self);
}

static void
lrg_combo_box_style_defaults (LrgWidget      *widget,
                              LrgUIPart       part,
                              LrgWidgetState  state,
                              LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);

	switch (part)
	{
	case LRG_UI_PART_BOX:
		{
			LrgThemeColor bg = LRG_THEME_COLOR_CONTROL;
			GrlColor      shadow;

			if (state & LRG_WIDGET_STATE_PRESSED)
				bg = LRG_THEME_COLOR_CONTROL_PRESSED;
			else if (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_OPEN))
				bg = LRG_THEME_COLOR_CONTROL_HOVER;

			lrg_part_look_set_background (look, lrg_theme_get_color (theme, bg));
			lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
			lrg_part_look_set_border (look,
			                          lrg_theme_get_color (theme, (state & LRG_WIDGET_STATE_OPEN)
			                                                      ? LRG_THEME_COLOR_ACCENT
			                                                      : LRG_THEME_COLOR_BORDER),
			                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
			lrg_part_look_set_radius (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS));

			/* A faint lift, a little deeper under the pointer */
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW),
			                            ((gfloat) lrg_theme_get_color (theme, LRG_THEME_COLOR_SHADOW)->a /
			                             255.0f) * 0.5f, &shadow);
			lrg_part_look_set_shadow (look, &shadow,
			                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_SHADOW_BLUR) *
			                          ((state & LRG_WIDGET_STATE_HOVERED) ? 0.4f : 0.25f),
			                          (state & LRG_WIDGET_STATE_PRESSED) ? 0.0f : 1.0f);

			if (state & LRG_WIDGET_STATE_DISABLED)
				lrg_part_look_set_opacity (look, 0.45f);
		}
		break;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		break;

	case LRG_UI_PART_ARROW:
		lrg_part_look_set_foreground (look,
		                              lrg_theme_get_color (theme,
		                                                   (state & LRG_WIDGET_STATE_OPEN)
		                                                   ? LRG_THEME_COLOR_ACCENT
		                                                   : (state & LRG_WIDGET_STATE_HOVERED)
		                                                     ? LRG_THEME_COLOR_TEXT
		                                                     : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE) * 0.5f);
		break;

	default:
		LRG_WIDGET_CLASS (lrg_combo_box_parent_class)->style_defaults (widget, part, state, look);
		break;
	}
}

static void
lrg_combo_box_measure (LrgWidget *widget,
                       gfloat    *width,
                       gfloat    *height)
{
	LrgComboBox *self = LRG_COMBO_BOX (widget);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	gfloat       spacing = lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING);
	gfloat       tw = text_width (theme, self->placeholder, size);
	guint        i;

	/* Wide enough for the longest item, so picking never resizes it */
	for (i = 0; i < self->items->len; i++)
		tw = MAX (tw, text_width (theme, g_ptr_array_index (self->items, i), size));

	*width = pad * 2.0f + tw + spacing + size;
	*height = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CONTROL_HEIGHT);
}

/* arrow_target: 1 while the list is open (chevron up), else 0 */
static gfloat
arrow_target (LrgComboBox *self)
{
	return (lrg_widget_get_state_flags (LRG_WIDGET (self)) & LRG_WIDGET_STATE_OPEN) ? 1.0f : 0.0f;
}

/*
 * lrg_combo_box_step:
 *
 * Advances the chevron flip towards the OPEN state by the canvas frame
 * time. The first step snaps it, so a box never animates in.
 *
 * Returns: %TRUE while the chevron is still turning
 */
static gboolean
lrg_combo_box_step (LrgWidget *widget,
                    gfloat     delta_time)
{
	LrgComboBox *self = LRG_COMBO_BOX (widget);

	lrg_spring_set_motion (&self->arrow, lrg_widget_get_motion (widget));
	lrg_spring_set_target (&self->arrow, arrow_target (self));
	if (!self->stepped || lrg_widget_get_motion (widget) == LRG_MOTION_NONE)
	{
		lrg_spring_snap (&self->arrow, self->arrow.target);
		self->stepped = TRUE;
		return FALSE;
	}

	return lrg_spring_step (&self->arrow, delta_time);
}

static void
lrg_combo_box_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	LrgComboBox *self = LRG_COMBO_BOX (widget);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	LrgPartLook  box;
	LrgPartLook  label;
	LrgPartLook  arrow;
	gfloat       x = lrg_widget_get_world_x (widget);
	gfloat       y = lrg_widget_get_world_y (widget);
	gfloat       w = lrg_widget_get_width (widget);
	gfloat       h = lrg_widget_get_height (widget);
	gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       pad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X);
	gfloat       spacing = lrg_theme_get_metric (theme, LRG_THEME_METRIC_SPACING);
	gfloat       cy = y + h * 0.5f;
	gfloat       flip;
	const gchar *text;

	/* Chevron flip: the spring once stepped, the state before that */
	flip = self->stepped ? self->arrow.value : arrow_target (self);

	/* Body */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, x, y, w, h);

	if (box.opacity < 1.0f)
		lrg_paint_list_push_opacity (list, box.opacity);

	/* Selected text (or the muted placeholder), clipped before the arrow */
	lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
	text = lrg_combo_box_get_selected_text (self);
	lrg_paint_list_push_clip (list, x + pad, y, MAX (0.0f, w - pad * 2.0f - spacing - size), h);
	if (text != NULL)
		paint_text_centered_v (list, theme, text, x + pad, cy, size, &label.foreground);
	else
		paint_text_centered_v (list, theme, self->placeholder, x + pad, cy, size,
		                       lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT_MUTED));
	lrg_paint_list_pop_clip (list);

	/* Chevron, rotated half a turn by the spring when open */
	lrg_widget_resolve_look (widget, LRG_UI_PART_ARROW, &arrow);
	{
		gfloat s = arrow.size > 0.0f ? arrow.size : size * 0.5f;
		gfloat ax = x + w - pad - size * 0.5f;
		gfloat angle = flip * (gfloat) G_PI;
		gfloat ca = cosf (angle);
		gfloat sa = sinf (angle);
		gfloat px[3] = { -s * 0.5f, 0.0f, s * 0.5f };
		gfloat py[3] = { -s * 0.25f, s * 0.25f, -s * 0.25f };
		gfloat rx[3];
		gfloat ry[3];
		gfloat t = MAX (1.5f, s * 0.2f);
		guint  k;

		for (k = 0; k < 3; k++)
		{
			rx[k] = ax + px[k] * ca - py[k] * sa;
			ry[k] = cy + px[k] * sa + py[k] * ca;
		}
		lrg_paint_list_line (list, rx[0], ry[0], rx[1], ry[1], t, &arrow.foreground);
		lrg_paint_list_line (list, rx[1], ry[1], rx[2], ry[2], t, &arrow.foreground);
	}

	if (box.opacity < 1.0f)
		lrg_paint_list_pop_opacity (list);
}

static gboolean
lrg_combo_box_handle_event (LrgWidget        *widget,
                            const LrgUIEvent *event)
{
	LrgComboBox *self = LRG_COMBO_BOX (widget);
	gboolean     inside = lrg_widget_contains_point (widget, lrg_ui_event_get_x (event),
	                                                 lrg_ui_event_get_y (event));
	gint         n = (gint) self->items->len;

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_POINTER_ENTER:
		self->swallow_press = FALSE;
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		if (lrg_ui_event_get_button (event) != 0 || !inside)
			return FALSE;
		/* Open on press; a press that just closed the list stays closed */
		if (self->swallow_press)
			self->swallow_press = FALSE;
		else if (self->popup != NULL)
			lrg_combo_box_popdown (self);
		else
			open_popup (self, FALSE);
		return TRUE;

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		return lrg_ui_event_get_button (event) == 0;

	case LRG_UI_EVENT_ACTIVATE:
		if (self->popup != NULL)
			lrg_combo_box_popdown (self);
		else
			open_popup (self, TRUE);
		return TRUE;

	case LRG_UI_EVENT_KEY_DOWN:
		if (self->popup != NULL || n == 0)
			return FALSE;
		/* Closed and focused: arrows step through the items */
		switch (lrg_ui_event_get_key (event))
		{
		case GRL_KEY_UP:
			lrg_combo_box_set_selected (self, self->selected < 0 ? 0 : MAX (self->selected - 1, 0));
			return TRUE;
		case GRL_KEY_DOWN:
			lrg_combo_box_set_selected (self, MIN (self->selected + 1, n - 1));
			return TRUE;
		case GRL_KEY_HOME:
			lrg_combo_box_set_selected (self, 0);
			return TRUE;
		case GRL_KEY_END:
			lrg_combo_box_set_selected (self, n - 1);
			return TRUE;
		default:
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
lrg_combo_box_dispose (GObject *object)
{
	LrgComboBox *self = LRG_COMBO_BOX (object);

	/* Close the list if it is still up */
	if (self->popup != NULL && self->canvas != NULL)
	{
		g_autoptr(LrgCanvas) canvas = g_object_ref (self->canvas);
		g_autoptr(LrgWidget) popup = LRG_WIDGET (g_object_ref (self->popup));

		forget_popup (self);
		lrg_canvas_dismiss_popup (canvas, popup);
	}
	forget_popup (self);

	G_OBJECT_CLASS (lrg_combo_box_parent_class)->dispose (object);
}

static void
lrg_combo_box_finalize (GObject *object)
{
	LrgComboBox *self = LRG_COMBO_BOX (object);

	g_clear_pointer (&self->items, g_ptr_array_unref);
	g_clear_pointer (&self->placeholder, g_free);

	G_OBJECT_CLASS (lrg_combo_box_parent_class)->finalize (object);
}

static void
lrg_combo_box_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgComboBox *self = LRG_COMBO_BOX (object);

	switch (prop_id)
	{
	case PROP_SELECTED:
		g_value_set_int (value, self->selected);
		break;
	case PROP_PLACEHOLDER:
		g_value_set_string (value, self->placeholder);
		break;
	case PROP_N_ITEMS:
		g_value_set_uint (value, self->items->len);
		break;
	case PROP_POPUP_OPEN:
		g_value_set_boolean (value, self->popup != NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_combo_box_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgComboBox *self = LRG_COMBO_BOX (object);

	switch (prop_id)
	{
	case PROP_SELECTED:
		lrg_combo_box_set_selected (self, g_value_get_int (value));
		break;
	case PROP_PLACEHOLDER:
		lrg_combo_box_set_placeholder (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_combo_box_class_init (LrgComboBoxClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass *widget_class = LRG_WIDGET_CLASS (klass);

	object_class->dispose = lrg_combo_box_dispose;
	object_class->finalize = lrg_combo_box_finalize;
	object_class->get_property = lrg_combo_box_get_property;
	object_class->set_property = lrg_combo_box_set_property;

	widget_class->style_defaults = lrg_combo_box_style_defaults;
	widget_class->measure = lrg_combo_box_measure;
	widget_class->paint = lrg_combo_box_paint;
	widget_class->step = lrg_combo_box_step;
	widget_class->handle_event = lrg_combo_box_handle_event;

	/**
	 * LrgComboBox:selected:
	 *
	 * Index of the selected item, or -1 when nothing is selected.
	 */
	properties[PROP_SELECTED] =
		g_param_spec_int ("selected", NULL, NULL, -1, G_MAXINT, -1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgComboBox:placeholder:
	 *
	 * Muted text shown when nothing is selected.
	 */
	properties[PROP_PLACEHOLDER] =
		g_param_spec_string ("placeholder", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgComboBox:n-items:
	 *
	 * Number of items.
	 */
	properties[PROP_N_ITEMS] =
		g_param_spec_uint ("n-items", NULL, NULL, 0, G_MAXUINT, 0,
		                   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgComboBox:popup-open:
	 *
	 * Whether the item list is showing.
	 */
	properties[PROP_POPUP_OPEN] =
		g_param_spec_boolean ("popup-open", NULL, NULL, FALSE,
		                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgComboBox::changed:
	 * @self: the combo box
	 *
	 * Emitted when #LrgComboBox:selected changes, whether by the user
	 * or by lrg_combo_box_set_selected().
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_combo_box_init (LrgComboBox *self)
{
	self->items = g_ptr_array_new_with_free_func (g_free);
	self->selected = -1;
	lrg_spring_init (&self->arrow, 0.0f, LRG_MOTION_SNAPPY);

	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
	g_signal_connect (self, "state-changed", G_CALLBACK (on_state_changed), NULL);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgComboBox *
lrg_combo_box_new (void)
{
	return g_object_new (LRG_TYPE_COMBO_BOX, NULL);
}

void
lrg_combo_box_append (LrgComboBox *self,
                      const gchar *text)
{
	g_return_if_fail (LRG_IS_COMBO_BOX (self));
	g_return_if_fail (text != NULL);

	/* An open list would no longer match the items */
	lrg_combo_box_popdown (self);
	g_ptr_array_add (self->items, g_strdup (text));
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_ITEMS]);
}

void
lrg_combo_box_remove_all (LrgComboBox *self)
{
	g_return_if_fail (LRG_IS_COMBO_BOX (self));

	if (self->items->len == 0)
		return;

	lrg_combo_box_popdown (self);
	g_ptr_array_set_size (self->items, 0);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_ITEMS]);
	lrg_combo_box_set_selected (self, -1);
}

guint
lrg_combo_box_get_n_items (LrgComboBox *self)
{
	g_return_val_if_fail (LRG_IS_COMBO_BOX (self), 0);

	return self->items->len;
}

const gchar *
lrg_combo_box_get_item_text (LrgComboBox *self,
                             guint        index)
{
	g_return_val_if_fail (LRG_IS_COMBO_BOX (self), NULL);

	if (index >= self->items->len)
		return NULL;
	return g_ptr_array_index (self->items, index);
}

gint
lrg_combo_box_get_selected (LrgComboBox *self)
{
	g_return_val_if_fail (LRG_IS_COMBO_BOX (self), -1);

	return self->selected;
}

void
lrg_combo_box_set_selected (LrgComboBox *self,
                            gint         index)
{
	g_return_if_fail (LRG_IS_COMBO_BOX (self));

	if (index < 0 || index >= (gint) self->items->len)
		index = -1;
	if (self->selected == index)
		return;

	self->selected = index;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTED]);
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

const gchar *
lrg_combo_box_get_selected_text (LrgComboBox *self)
{
	g_return_val_if_fail (LRG_IS_COMBO_BOX (self), NULL);

	if (self->selected < 0)
		return NULL;
	return g_ptr_array_index (self->items, (guint) self->selected);
}

const gchar *
lrg_combo_box_get_placeholder (LrgComboBox *self)
{
	g_return_val_if_fail (LRG_IS_COMBO_BOX (self), NULL);

	return self->placeholder;
}

void
lrg_combo_box_set_placeholder (LrgComboBox *self,
                               const gchar *placeholder)
{
	g_return_if_fail (LRG_IS_COMBO_BOX (self));

	if (g_strcmp0 (self->placeholder, placeholder) == 0)
		return;
	g_free (self->placeholder);
	self->placeholder = g_strdup (placeholder);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLACEHOLDER]);
}

void
lrg_combo_box_popup (LrgComboBox *self)
{
	g_return_if_fail (LRG_IS_COMBO_BOX (self));

	open_popup (self, FALSE);
}

void
lrg_combo_box_popdown (LrgComboBox *self)
{
	g_return_if_fail (LRG_IS_COMBO_BOX (self));

	if (self->popup == NULL)
		return;

	if (self->canvas != NULL)
	{
		g_autoptr(LrgCanvas) canvas = g_object_ref (self->canvas);
		g_autoptr(LrgWidget) popup = LRG_WIDGET (g_object_ref (self->popup));

		/* The canvas emits popup-dismissed, which forgets the popup */
		lrg_canvas_dismiss_popup (canvas, popup);
	}
	forget_popup (self);
}

gboolean
lrg_combo_box_get_popup_open (LrgComboBox *self)
{
	g_return_val_if_fail (LRG_IS_COMBO_BOX (self), FALSE);

	return self->popup != NULL;
}
