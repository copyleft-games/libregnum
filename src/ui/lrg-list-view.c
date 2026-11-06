/* lrg-list-view.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgListView: A virtualized, scrollable list of rows built from a GListModel.
 *
 * Only the rows inside the viewport (plus one buffer row above and
 * below) exist as widgets. As the list scrolls, rows leaving the range
 * are rebound to the items entering it, so a model with 100,000 items
 * costs as many widgets as fit on screen. Rows are uniform in height.
 *
 * Scrolling shares the spring / rubber-band / overlay scrollbar code
 * with LrgScrollBox (lrg-scrollbar-private.h). The view draws row
 * backgrounds itself (ITEM part: hover, press, selection) under the
 * row widgets, and a cursor outline while focus is visible.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-list-view.h"
#include "lrg-label.h"
#include "lrg-scrollbar-private.h"
#include "../lrg-log.h"

/* Rows kept beyond each end of the visible range */
#define ROW_BUFFER (1)

/* Two presses on one row within this time (seconds of canvas clock)
 * are a double click */
#define DOUBLE_CLICK_TIME (0.4)

/* Inset of the row background inside the viewport */
#define ROW_INSET (2.0f)

/* Preferred size when nothing else says otherwise */
#define NATURAL_WIDTH (220.0f)
#define NATURAL_ROWS  (8)

typedef struct
{
	LrgWidget *widget;    /* owned */
	guint      position;  /* bound item, or LRG_LIST_VIEW_INVALID_POSITION */
	gfloat     dy;        /* row y inside its slot, from the last placement */
	gboolean   placed;    /* dy is valid for the current width */
} Row;

struct _LrgListView
{
	LrgContainer           parent_instance;

	GListModel            *model;
	gulong                 items_changed_id;

	LrgListViewCreateFunc  create;
	LrgListViewBindFunc    bind;
	gpointer               user_data;
	GDestroyNotify         destroy;

	GArray                *rows;        /* Row */
	gfloat                 row_height;  /* 0 = theme ROW_HEIGHT */
	gfloat                 placed_width;

	LrgSelectionMode       mode;
	GHashTable            *selected;    /* set of GUINT_TO_POINTER (pos + 1) */
	guint                  cursor;
	guint                  anchor;

	LrgScrollAxis          ay;          /* ay.bar owned */
	LrgScrollFx            fx;

	gdouble                clock;           /* sum of step() frame times */
	gboolean               click_armed;     /* a first click may double */
	gdouble                last_click_time; /* clock at that click */
	guint                  last_click_pos;
	gboolean               in_layout;
};

G_DEFINE_FINAL_TYPE (LrgListView, lrg_list_view, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_MODEL,
	PROP_ROW_HEIGHT,
	PROP_SELECTION_MODE,
	N_PROPS
};

enum
{
	SIGNAL_SELECTION_CHANGED,
	SIGNAL_ROW_ACTIVATED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

#define KEY(pos) (GUINT_TO_POINTER ((pos) + 1))

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static guint
n_items (LrgListView *self)
{
	return self->model != NULL ? g_list_model_get_n_items (self->model) : 0;
}

static gfloat
row_h (LrgListView *self)
{
	gfloat h = self->row_height;

	if (h <= 0.0f)
		h = lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_METRIC_ROW_HEIGHT);
	return MAX (h, 1.0f);
}

/* viewport: padding and content size, relative */
static void
viewport (LrgListView *self,
          gfloat      *pad,
          gfloat      *vw,
          gfloat      *vh)
{
	LrgWidget *w = LRG_WIDGET (self);

	*pad = lrg_container_get_padding (LRG_CONTAINER (self));
	*vw = MAX (0.0f, lrg_widget_get_width (w) - *pad * 2.0f);
	*vh = MAX (0.0f, lrg_widget_get_height (w) - *pad * 2.0f);
}

static gboolean
smooth (LrgListView *self)
{
	return lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;
}

/* rows_per_page: whole rows that fit, at least one */
static guint
rows_per_page (LrgListView *self)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;

	viewport (self, &pad, &vw, &vh);
	return MAX (1u, (guint) floorf (vh / row_h (self)));
}

/* update_extent: content length from the item count */
static void
update_extent (LrgListView *self)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;

	viewport (self, &pad, &vw, &vh);
	lrg_scroll_axis_set_extent (&self->ay, (gfloat) n_items (self) * row_h (self), vh);
}

/* ==========================================================================
 * Default factory
 * ========================================================================== */

static LrgWidget *
default_create (LrgListView *self,
                gpointer     user_data)
{
	LrgLabel *label = lrg_label_new ("");

	/* Vertically centred in the row, full width */
	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	return LRG_WIDGET (label);
}

/*
 * default_bind:
 *
 * Shows the first string property among string / text / name / label
 * / title, else the item's type name.
 */
static void
default_bind (LrgListView *self,
              LrgWidget   *row,
              GObject     *item,
              guint        position,
              gpointer     user_data)
{
	static const gchar * const names[] = { "string", "text", "name", "label", "title" };
	g_autofree gchar          *text = NULL;
	GObjectClass              *klass;
	guint                      i;

	if (!LRG_IS_LABEL (row))
		return;

	klass = G_OBJECT_GET_CLASS (item);
	for (i = 0; i < G_N_ELEMENTS (names) && text == NULL; i++)
	{
		GParamSpec *pspec = g_object_class_find_property (klass, names[i]);

		if (pspec != NULL && pspec->value_type == G_TYPE_STRING && (pspec->flags & G_PARAM_READABLE))
			g_object_get (item, names[i], &text, NULL);
	}

	lrg_label_set_text (LRG_LABEL (row), text != NULL ? text : G_OBJECT_TYPE_NAME (item));
}

/* ==========================================================================
 * Row pool
 * ========================================================================== */

/*
 * grow_pool:
 *
 * Adds @n new rows to the pool, then re-adds the scrollbar zone so it
 * stays the last child (hit before the rows). Layout is suppressed.
 */
static void
grow_pool (LrgListView *self,
           guint        n)
{
	LrgContainer *c = LRG_CONTAINER (self);
	gboolean      was = self->in_layout;
	guint         i;

	if (n == 0)
		return;

	self->in_layout = TRUE;
	lrg_container_remove_child (c, self->ay.bar);
	for (i = 0; i < n; i++)
	{
		Row        row;
		LrgWidget *w = self->create != NULL ? self->create (self, self->user_data)
		                                    : default_create (self, NULL);

		if (w == NULL)
		{
			g_warning ("LrgListView: row factory returned NULL");
			break;
		}
		row.widget = w;
		row.position = LRG_LIST_VIEW_INVALID_POSITION;
		row.dy = 0.0f;
		row.placed = FALSE;
		lrg_widget_set_visible (w, FALSE);
		lrg_container_add_child (c, w);
		g_array_append_val (self->rows, row);
	}
	lrg_container_add_child (c, self->ay.bar);
	self->in_layout = was;
}

/* drop_pool: removes every row widget (factory change, dispose) */
static void
drop_pool (LrgListView *self)
{
	gboolean was = self->in_layout;
	guint    i;

	self->in_layout = TRUE;
	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		if (lrg_widget_get_parent (r->widget) == LRG_CONTAINER (self))
			lrg_container_remove_child (LRG_CONTAINER (self), r->widget);
		g_clear_object (&r->widget);
	}
	g_array_set_size (self->rows, 0);
	self->in_layout = was;
}

/* unbind_from: forget bindings at or after @position (model changed) */
static void
unbind_from (LrgListView *self,
             guint        position)
{
	guint i;

	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		if (r->position != LRG_LIST_VIEW_INVALID_POSITION && r->position >= position)
			r->position = LRG_LIST_VIEW_INVALID_POSITION;
	}
}

/* bind_row: point @r at @position and let the factory fill it */
static void
bind_row (LrgListView *self,
          Row         *r,
          guint        position)
{
	g_autoptr(GObject) item = g_list_model_get_item (self->model, position);

	r->position = position;
	r->placed = FALSE;
	if (item == NULL)
		return;
	if (self->bind != NULL)
		self->bind (self, r->widget, item, position, self->user_data);
	else
		default_bind (self, r->widget, item, position, NULL);
}

/*
 * sync_rows:
 *
 * The heart of virtualization. Works out the range of positions that
 * must exist for the displayed offset (visible rows plus ROW_BUFFER on
 * each side), keeps rows already bound inside it, rebinds the others
 * to the missing positions, hides leftovers, and positions every row.
 */
static void
sync_rows (LrgListView *self)
{
	guint  n = n_items (self);
	gfloat rh = row_h (self);
	gfloat pad;
	gfloat vw;
	gfloat vh;
	gfloat off;
	gfloat slot_x;
	gfloat slot_w;
	guint  first;
	guint  start;
	guint  end;
	guint  p;
	guint  i;

	viewport (self, &pad, &vw, &vh);
	off = lrg_scroll_axis_get_value (&self->ay);

	/* 1. Range of positions to realize */
	first = off > 0.0f ? (guint) floorf (off / rh) : 0;
	start = first > ROW_BUFFER ? first - ROW_BUFFER : 0;
	end = MIN (n, first + (guint) ceilf (vh / rh) + ROW_BUFFER);
	if (start > end)
		start = end;

	/* 2. Enough widgets for it */
	if (self->rows->len < end - start)
		grow_pool (self, (end - start) - self->rows->len);

	/* 3. Free rows bound outside the range */
	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		if (r->position != LRG_LIST_VIEW_INVALID_POSITION && (r->position < start || r->position >= end))
			r->position = LRG_LIST_VIEW_INVALID_POSITION;
	}

	/* 4. Bind free rows to the positions nobody shows yet */
	for (p = start; p < end; p++)
	{
		Row     *free_row = NULL;
		gboolean shown = FALSE;

		for (i = 0; i < self->rows->len && !shown; i++)
		{
			Row *r = &g_array_index (self->rows, Row, i);

			if (r->position == p)
				shown = TRUE;
			else if (free_row == NULL && r->position == LRG_LIST_VIEW_INVALID_POSITION)
				free_row = r;
		}
		if (!shown && free_row != NULL)
			bind_row (self, free_row, p);
	}

	/* 5. Place, show / hide, mark selection */
	slot_x = pad + ROW_INSET + lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)),
	                                                 LRG_THEME_METRIC_PADDING_X) * 0.5f;
	slot_w = MAX (0.0f, vw - (slot_x - pad) * 2.0f);
	if (slot_w != self->placed_width)
	{
		self->placed_width = slot_w;
		for (i = 0; i < self->rows->len; i++)
			g_array_index (self->rows, Row, i).placed = FALSE;
	}

	for (i = 0; i < self->rows->len; i++)
	{
		Row   *r = &g_array_index (self->rows, Row, i);
		gfloat slot_y;

		if (r->position == LRG_LIST_VIEW_INVALID_POSITION)
		{
			lrg_widget_set_visible (r->widget, FALSE);
			continue;
		}

		lrg_widget_set_visible (r->widget, TRUE);
		slot_y = pad + (gfloat) r->position * rh - off;

		/* Full placement (measures) only after binding or resizing;
		 * plain scrolling just moves the row */
		if (!r->placed)
		{
			lrg_widget_place_in_slot (r->widget, slot_x, slot_y, slot_w, rh);
			r->dy = lrg_widget_get_y (r->widget) - slot_y;
			r->placed = TRUE;
		}
		else
		{
			lrg_widget_set_y (r->widget, slot_y + r->dy);
		}

		if (g_hash_table_contains (self->selected, KEY (r->position)))
			lrg_widget_set_state_flags (r->widget, LRG_WIDGET_STATE_SELECTED);
		else
			lrg_widget_unset_state_flags (r->widget, LRG_WIDGET_STATE_SELECTED);
	}
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

static void
selection_changed (LrgListView *self)
{
	g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

/* set_cursor: move the cursor (and keep it on screen) */
static void
set_cursor (LrgListView *self,
            guint        position,
            gboolean     reveal)
{
	self->cursor = position;
	if (reveal && position != LRG_LIST_VIEW_INVALID_POSITION)
		lrg_list_view_scroll_to (self, position, TRUE);
}

/* select_only: the selection becomes exactly @position (if allowed) */
static void
select_only (LrgListView *self,
             guint        position)
{
	if (self->mode == LRG_SELECTION_MODE_NONE)
		return;
	if (g_hash_table_size (self->selected) == 1 && g_hash_table_contains (self->selected, KEY (position)))
		return;
	g_hash_table_remove_all (self->selected);
	g_hash_table_add (self->selected, KEY (position));
	selection_changed (self);
}

/* select_range: the selection becomes [a, b] (multiple mode) */
static void
select_range (LrgListView *self,
              guint        a,
              guint        b)
{
	guint lo = MIN (a, b);
	guint hi = MAX (a, b);
	guint p;

	g_hash_table_remove_all (self->selected);
	for (p = lo; p <= hi; p++)
		g_hash_table_add (self->selected, KEY (p));
	selection_changed (self);
}

/*
 * move_cursor_to:
 *
 * Keyboard movement: cursor to @position, selection follows (Shift
 * extends from the anchor in multiple mode).
 */
static void
move_cursor_to (LrgListView *self,
                guint        position,
                gboolean     extend)
{
	guint n = n_items (self);

	if (n == 0)
		return;
	position = MIN (position, n - 1);

	if (extend && self->mode == LRG_SELECTION_MODE_MULTIPLE && self->anchor != LRG_LIST_VIEW_INVALID_POSITION)
		select_range (self, self->anchor, position);
	else
	{
		self->anchor = position;
		select_only (self, position);
	}
	set_cursor (self, position, TRUE);
}

/* ==========================================================================
 * Model
 * ========================================================================== */

/*
 * on_items_changed:
 *
 * Shifts the selection and cursor past the edit, drops selected items
 * that were removed, and rebinds rows at or after @position.
 */
static void
on_items_changed (GListModel  *model,
                  guint        position,
                  guint        removed,
                  guint        added,
                  LrgListView *self)
{
	GHashTable     *moved;
	GHashTableIter  iter;
	gpointer        key;
	gboolean        changed = FALSE;
	guint           n = n_items (self);

	moved = g_hash_table_new (g_direct_hash, g_direct_equal);
	g_hash_table_iter_init (&iter, self->selected);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		guint p = GPOINTER_TO_UINT (key) - 1;

		if (p < position)
			g_hash_table_add (moved, KEY (p));
		else if (p < position + removed)
			changed = TRUE;
		else
		{
			g_hash_table_add (moved, KEY (p - removed + added));
			changed |= removed != added;
		}
	}
	g_hash_table_unref (self->selected);
	self->selected = moved;

	/* Cursor and anchor follow their item, or land at the edit */
	if (self->cursor != LRG_LIST_VIEW_INVALID_POSITION && self->cursor >= position)
	{
		if (self->cursor >= position + removed)
			self->cursor = self->cursor - removed + added;
		else
			self->cursor = n > 0 ? MIN (position, n - 1) : LRG_LIST_VIEW_INVALID_POSITION;
	}
	if (self->cursor != LRG_LIST_VIEW_INVALID_POSITION && self->cursor >= n)
		self->cursor = n > 0 ? n - 1 : LRG_LIST_VIEW_INVALID_POSITION;
	self->anchor = self->cursor;

	unbind_from (self, position);
	update_extent (self);
	sync_rows (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));

	if (changed)
		selection_changed (self);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_list_view_layout_children (LrgContainer *container)
{
	LrgListView *self = LRG_LIST_VIEW (container);
	gfloat       pad;
	gfloat       vw;
	gfloat       vh;
	gfloat       zone;

	if (self->in_layout || self->ay.bar == NULL)
		return;
	self->in_layout = TRUE;

	viewport (self, &pad, &vw, &vh);
	update_extent (self);

	/* Scrollbar zone along the right edge, only when it can scroll */
	zone = lrg_scroll_bar_zone (LRG_WIDGET (self));
	lrg_widget_set_visible (self->ay.bar, lrg_scroll_axis_get_max (&self->ay) > 0.0f);
	lrg_widget_allocate (self->ay.bar, pad + vw - zone, pad, zone, vh);

	sync_rows (self);
	self->in_layout = FALSE;
}

static void
lrg_list_view_measure (LrgWidget *widget,
                       gfloat    *preferred_width,
                       gfloat    *preferred_height)
{
	LrgListView *self = LRG_LIST_VIEW (widget);
	gfloat       pad = lrg_container_get_padding (LRG_CONTAINER (self));
	guint        rows = MIN (n_items (self), NATURAL_ROWS);

	/* Measuring every item would defeat virtualization: a fixed
	 * natural width and up to NATURAL_ROWS rows */
	*preferred_width = NATURAL_WIDTH + pad * 2.0f;
	*preferred_height = (gfloat) MAX (rows, 1u) * row_h (self) + pad * 2.0f;
}

/* ==========================================================================
 * Looks and painting
 * ========================================================================== */

static void
lrg_list_view_style_defaults (LrgWidget      *widget,
                              LrgUIPart       part,
                              LrgWidgetState  state,
                              LrgPartLook    *look)
{
	LrgTheme *theme = lrg_widget_get_theme (widget);
	gfloat    radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	GrlColor  c;

	if (lrg_scroll_style_defaults (theme, part, state, look))
		return;

	switch (part)
	{
	case LRG_UI_PART_BOX:
		/* A recessed well like an entry */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme,
		                          (state & LRG_WIDGET_STATE_FOCUSED) ? LRG_THEME_COLOR_ACCENT
		                                                             : LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, radius);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_ITEM:
		/* Row backgrounds: clear at rest, control hover, selection
		 * tint (a little stronger when hovered), pressed control */
		if (state & LRG_WIDGET_STATE_SELECTED)
		{
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_SELECTION);
			if (state & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED))
				c.a = (guint8) MIN (255, (gint) c.a + 40);
		}
		else if (state & LRG_WIDGET_STATE_PRESSED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED);
		else if (state & LRG_WIDGET_STATE_HOVERED)
			c = *lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER);
		else
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER), 0.0f, &c);
		lrg_part_look_set_background (look, &c);
		lrg_part_look_set_radius (look, MAX (2.0f, radius - ROW_INSET));
		return;

	case LRG_UI_PART_INDICATOR:
		/* Keyboard cursor outline */
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_FOCUS_RING_WIDTH) * 0.75f);
		lrg_part_look_set_radius (look, MAX (2.0f, radius - ROW_INSET));
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_list_view_parent_class)->style_defaults (widget, part, state, look);
}

/* paint_row_background: the ITEM look of one row, blended by its springs */
static void
paint_row_background (LrgListView  *self,
                      LrgPaintList *list,
                      const Row    *r,
                      gfloat        x,
                      gfloat        y,
                      gfloat        w,
                      gfloat        h)
{
	LrgWidget     *widget = LRG_WIDGET (self);
	LrgWidgetState base = LRG_WIDGET_STATE_NONE;
	LrgPartLook    look;
	LrgPartLook    variant;
	gfloat         hover = CLAMP (lrg_widget_get_hover_progress (r->widget), 0.0f, 1.0f);
	gfloat         press = CLAMP (lrg_widget_get_press_progress (r->widget), 0.0f, 1.0f);

	if (g_hash_table_contains (self->selected, KEY (r->position)))
		base |= LRG_WIDGET_STATE_SELECTED;

	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, base, &look);
	if (hover > 0.001f)
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM, base | LRG_WIDGET_STATE_HOVERED, &variant);
		lrg_part_look_lerp (&look, &variant, hover, &look);
	}
	if (press > 0.001f)
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ITEM,
		                                   base | LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED,
		                                   &variant);
		lrg_part_look_lerp (&look, &variant, press, &look);
	}
	lrg_widget_paint_box (widget, list, &look, x, y, w, h);

	/* Cursor outline after keyboard / gamepad navigation */
	if (r->position == self->cursor &&
	    (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_FOCUS_VISIBLE))
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_INDICATOR, base, &variant);
		lrg_paint_list_rect_ex (list, x, y, w, h, &variant.radii, NULL, NULL,
		                        variant.border_width, &variant.border);
	}
}

/*
 * lrg_list_view_step:
 *
 * Advances the canvas clock (double-click timing), the scroll spring
 * (keeping the bar shown while it moves) and the bar fade, then binds
 * rows for the new offset so they are stepped and painted this frame.
 *
 * Returns: %TRUE while the offset glides / bounces or the bar fades
 *   (or waits to fade out)
 */
static gboolean
lrg_list_view_step (LrgWidget *widget,
                    gfloat     delta_time)
{
	LrgListView *self = LRG_LIST_VIEW (widget);
	gboolean     moving;
	gboolean     keep;

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;
	if (self->ay.bar == NULL)
		return FALSE;

	moving = lrg_scroll_axis_step_snap (&self->ay, delta_time);
	if (moving)
		lrg_scroll_fx_poke (&self->fx);
	sync_rows (self);

	keep = self->ay.dragging || (lrg_widget_get_state_flags (self->ay.bar) & LRG_WIDGET_STATE_HOVERED);
	return lrg_scroll_fx_update (&self->fx, keep, delta_time) || moving;
}

static void
lrg_list_view_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	LrgListView *self = LRG_LIST_VIEW (widget);
	LrgPartLook  box;
	gfloat       wx = lrg_widget_get_world_x (widget);
	gfloat       wy = lrg_widget_get_world_y (widget);
	gfloat       rh = row_h (self);
	gfloat       pad;
	gfloat       vw;
	gfloat       vh;
	gfloat       off;
	guint        i;

	/* 1. Rows follow the displayed offset. The spring moves only in
	 *    step(); this just binds rows for jumps made since. */
	sync_rows (self);

	/* 2. Body */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, wx, wy,
	                      lrg_widget_get_width (widget), lrg_widget_get_height (widget));

	/* 3. Rows, clipped to the viewport: background, then the widget */
	viewport (self, &pad, &vw, &vh);
	off = lrg_scroll_axis_get_value (&self->ay);
	lrg_paint_list_push_clip (list, wx + pad, wy + pad, vw, vh);
	for (i = 0; i < self->rows->len; i++)
	{
		const Row *r = &g_array_index (self->rows, Row, i);
		gfloat     ry;

		if (r->position == LRG_LIST_VIEW_INVALID_POSITION)
			continue;
		ry = wy + pad + (gfloat) r->position * rh - off;
		paint_row_background (self, list, r, wx + pad + ROW_INSET, ry + 1.0f,
		                      MAX (0.0f, vw - ROW_INSET * 2.0f), MAX (0.0f, rh - 2.0f));
		lrg_widget_paint (r->widget, list);
	}
	lrg_paint_list_pop_clip (list);

	/* 4. Overlay scrollbar */
	lrg_scroll_bar_paint (widget, list, &self->ay, TRUE, self->fx.fade.value);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* position_at: the row under a world y inside the viewport, or invalid */
static guint
position_at (LrgListView *self,
             gfloat       y)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;
	gfloat rel;
	guint  p;

	viewport (self, &pad, &vw, &vh);
	rel = y - (lrg_widget_get_world_y (LRG_WIDGET (self)) + pad);
	if (rel < 0.0f || rel >= vh)
		return LRG_LIST_VIEW_INVALID_POSITION;

	rel += lrg_scroll_axis_get_value (&self->ay);
	if (rel < 0.0f)
		return LRG_LIST_VIEW_INVALID_POSITION;
	p = (guint) floorf (rel / row_h (self));
	return p < n_items (self) ? p : LRG_LIST_VIEW_INVALID_POSITION;
}

/*
 * click_row:
 *
 * Plain click selects only the row; Ctrl toggles it and Shift selects
 * the range from the anchor (multiple mode). A second click on the
 * same row within DOUBLE_CLICK_TIME (canvas clock) activates it.
 */
static void
click_row (LrgListView       *self,
           guint              p,
           LrgInputModifiers  mods)
{
	if (self->mode == LRG_SELECTION_MODE_MULTIPLE && (mods & LRG_INPUT_MODIFIER_SHIFT) &&
	    self->anchor != LRG_LIST_VIEW_INVALID_POSITION)
	{
		select_range (self, self->anchor, p);
	}
	else if (self->mode == LRG_SELECTION_MODE_MULTIPLE && (mods & LRG_INPUT_MODIFIER_CTRL))
	{
		if (!g_hash_table_remove (self->selected, KEY (p)))
			g_hash_table_add (self->selected, KEY (p));
		self->anchor = p;
		selection_changed (self);
	}
	else
	{
		self->anchor = p;
		select_only (self, p);
	}
	set_cursor (self, p, FALSE);

	if (self->click_armed && self->last_click_pos == p &&
	    self->clock - self->last_click_time <= DOUBLE_CLICK_TIME)
	{
		self->click_armed = FALSE;
		g_signal_emit (self, signals[SIGNAL_ROW_ACTIVATED], 0, p);
		return;
	}
	self->click_armed = TRUE;
	self->last_click_time = self->clock;
	self->last_click_pos = p;
}

static gboolean
handle_key (LrgListView       *self,
            GrlKey             key,
            LrgInputModifiers  mods)
{
	guint    n = n_items (self);
	guint    cur = self->cursor;
	guint    page = rows_per_page (self);
	gboolean extend = (mods & LRG_INPUT_MODIFIER_SHIFT) != 0;
	gboolean none = cur == LRG_LIST_VIEW_INVALID_POSITION;

	switch (key)
	{
	case GRL_KEY_UP:
		/* Arrows are kept even at the ends so focus does not jump out */
		if (n > 0)
			move_cursor_to (self, none ? 0 : (cur > 0 ? cur - 1 : 0), extend);
		return TRUE;
	case GRL_KEY_DOWN:
		if (n > 0)
			move_cursor_to (self, none ? 0 : cur + 1, extend);
		return TRUE;
	case GRL_KEY_HOME:
		if (n > 0)
			move_cursor_to (self, 0, extend);
		return TRUE;
	case GRL_KEY_END:
		if (n > 0)
			move_cursor_to (self, n - 1, extend);
		return TRUE;
	case GRL_KEY_PAGE_UP:
		if (n > 0)
			move_cursor_to (self, none ? 0 : (cur > page ? cur - page : 0), extend);
		return TRUE;
	case GRL_KEY_PAGE_DOWN:
		if (n > 0)
			move_cursor_to (self, none ? MIN (page, n - 1) : cur + page, extend);
		return TRUE;
	default:
		return FALSE;
	}
}

static gboolean
lrg_list_view_handle_event (LrgWidget        *widget,
                            const LrgUIEvent *event)
{
	LrgListView *self = LRG_LIST_VIEW (widget);
	LrgWidget   *target = lrg_ui_event_get_target (event);
	gfloat       y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_POINTER_ENTER:
		lrg_scroll_fx_poke (&self->fx);
		return FALSE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		lrg_scroll_fx_poke (&self->fx);
		if (self->ay.dragging)
		{
			lrg_scroll_bar_drag (&self->ay, TRUE, y);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			guint p;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			if (target == self->ay.bar)
			{
				lrg_scroll_bar_press (&self->ay, TRUE, y, smooth (self));
				lrg_scroll_fx_poke (&self->fx);
				return TRUE;
			}
			p = position_at (self, y);
			if (p == LRG_LIST_VIEW_INVALID_POSITION)
				return FALSE;
			click_row (self, p, lrg_ui_event_get_modifiers (event));
			return TRUE;
		}

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (self->ay.dragging)
		{
			self->ay.dragging = FALSE;
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_PAN:
		/* Vertical touch drags scroll the list, following the finger */
		update_extent (self);
		if (fabsf (lrg_ui_event_get_delta_y (event)) < fabsf (lrg_ui_event_get_delta_x (event)) * 0.5f)
			return FALSE;
		if (!lrg_scroll_axis_pan (&self->ay, lrg_ui_event_get_delta_y (event)))
			return FALSE;
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case LRG_UI_EVENT_PAN_END:
		lrg_scroll_axis_flick (&self->ay, smooth (self) ? lrg_ui_event_get_velocity_y (event) : 0.0f);
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case LRG_UI_EVENT_SCROLL:
		{
			gboolean handled;

			update_extent (self);
			handled = lrg_scroll_axis_wheel (&self->ay, -lrg_ui_event_get_scroll_y (event) *
			                                 LRG_SCROLL_WHEEL_STEP, smooth (self));
			if (handled)
				lrg_scroll_fx_poke (&self->fx);
			return handled;
		}

	case LRG_UI_EVENT_KEY_DOWN:
		update_extent (self);
		return handle_key (self, lrg_ui_event_get_key (event), lrg_ui_event_get_modifiers (event));

	case LRG_UI_EVENT_ACTIVATE:
		if (self->cursor == LRG_LIST_VIEW_INVALID_POSITION || self->cursor >= n_items (self))
			return FALSE;
		g_signal_emit (self, signals[SIGNAL_ROW_ACTIVATED], 0, self->cursor);
		return TRUE;

	default:
		return FALSE;
	}
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
clear_factory (LrgListView *self)
{
	if (self->destroy != NULL && self->user_data != NULL)
		self->destroy (self->user_data);
	self->create = NULL;
	self->bind = NULL;
	self->user_data = NULL;
	self->destroy = NULL;
}

static void
lrg_list_view_constructed (GObject *object)
{
	LrgListView *self = LRG_LIST_VIEW (object);

	G_OBJECT_CLASS (lrg_list_view_parent_class)->constructed (object);

	self->in_layout = TRUE;
	self->ay.bar = lrg_scroll_bar_new ("vscrollbar");
	lrg_container_add_child (LRG_CONTAINER (self), self->ay.bar);
	self->in_layout = FALSE;
}

static void
lrg_list_view_dispose (GObject *object)
{
	LrgListView *self = LRG_LIST_VIEW (object);

	if (self->model != NULL)
	{
		g_clear_signal_handler (&self->items_changed_id, self->model);
		g_clear_object (&self->model);
	}
	if (self->rows != NULL)
		drop_pool (self);
	clear_factory (self);
	g_clear_object (&self->ay.bar);

	G_OBJECT_CLASS (lrg_list_view_parent_class)->dispose (object);
}

static void
lrg_list_view_finalize (GObject *object)
{
	LrgListView *self = LRG_LIST_VIEW (object);

	g_clear_pointer (&self->rows, g_array_unref);
	g_clear_pointer (&self->selected, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_list_view_parent_class)->finalize (object);
}

static void
lrg_list_view_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgListView *self = LRG_LIST_VIEW (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		g_value_set_object (value, self->model);
		break;
	case PROP_ROW_HEIGHT:
		g_value_set_float (value, self->row_height);
		break;
	case PROP_SELECTION_MODE:
		g_value_set_enum (value, self->mode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_list_view_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgListView *self = LRG_LIST_VIEW (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		lrg_list_view_set_model (self, g_value_get_object (value));
		break;
	case PROP_ROW_HEIGHT:
		lrg_list_view_set_row_height (self, g_value_get_float (value));
		break;
	case PROP_SELECTION_MODE:
		lrg_list_view_set_selection_mode (self, (LrgSelectionMode) g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_list_view_class_init (LrgListViewClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->constructed = lrg_list_view_constructed;
	object_class->dispose = lrg_list_view_dispose;
	object_class->finalize = lrg_list_view_finalize;
	object_class->get_property = lrg_list_view_get_property;
	object_class->set_property = lrg_list_view_set_property;

	widget_class->measure = lrg_list_view_measure;
	widget_class->paint = lrg_list_view_paint;
	widget_class->step = lrg_list_view_step;
	widget_class->style_defaults = lrg_list_view_style_defaults;
	widget_class->handle_event = lrg_list_view_handle_event;

	container_class->layout_children = lrg_list_view_layout_children;

	/**
	 * LrgListView:model:
	 *
	 * The #GListModel whose items are shown.
	 */
	properties[PROP_MODEL] =
		g_param_spec_object ("model", "Model", "The items to show",
		                     G_TYPE_LIST_MODEL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgListView:row-height:
	 *
	 * Height of every row; 0 uses the theme's ROW_HEIGHT metric.
	 */
	properties[PROP_ROW_HEIGHT] =
		g_param_spec_float ("row-height", "Row height", "Height of every row (0 = theme)",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgListView:selection-mode:
	 *
	 * How many rows can be selected.
	 */
	properties[PROP_SELECTION_MODE] =
		g_param_spec_enum ("selection-mode", "Selection mode", "How many rows can be selected",
		                   LRG_TYPE_SELECTION_MODE, LRG_SELECTION_MODE_SINGLE,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgListView::selection-changed:
	 * @self: the list view
	 *
	 * Emitted when rows were selected or unselected.
	 */
	signals[SIGNAL_SELECTION_CHANGED] =
		g_signal_new ("selection-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgListView::row-activated:
	 * @self: the list view
	 * @position: the activated row
	 *
	 * Emitted on a double click, or Enter / Space / gamepad A on the
	 * cursor row.
	 */
	signals[SIGNAL_ROW_ACTIVATED] =
		g_signal_new ("row-activated",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_UINT);

	/* Positions children with a meaning of their own (scroll offsets,
	 * columns, painted chrome): no automatic RTL mirroring */
	lrg_widget_class_set_handles_direction (LRG_WIDGET_CLASS (klass), TRUE);
}

static void
lrg_list_view_init (LrgListView *self)
{
	self->rows = g_array_new (FALSE, TRUE, sizeof (Row));
	self->selected = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->mode = LRG_SELECTION_MODE_SINGLE;
	self->cursor = LRG_LIST_VIEW_INVALID_POSITION;
	self->anchor = LRG_LIST_VIEW_INVALID_POSITION;
	self->last_click_pos = LRG_LIST_VIEW_INVALID_POSITION;
	self->placed_width = -1.0f;
	lrg_scroll_axis_init (&self->ay);
	lrg_scroll_fx_init (&self->fx);

	lrg_container_set_padding (LRG_CONTAINER (self), 4.0f);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgListView *
lrg_list_view_new (void)
{
	return g_object_new (LRG_TYPE_LIST_VIEW, NULL);
}

void
lrg_list_view_set_model (LrgListView *self,
                         GListModel  *model)
{
	gboolean had_selection;

	g_return_if_fail (LRG_IS_LIST_VIEW (self));
	g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

	if (self->model == model)
		return;

	if (self->model != NULL)
	{
		g_clear_signal_handler (&self->items_changed_id, self->model);
		g_clear_object (&self->model);
	}
	if (model != NULL)
	{
		self->model = g_object_ref (model);
		self->items_changed_id = g_signal_connect (model, "items-changed",
		                                           G_CALLBACK (on_items_changed), self);
	}

	/* Fresh start: no selection, cursor at the top, scrolled up */
	had_selection = g_hash_table_size (self->selected) > 0;
	g_hash_table_remove_all (self->selected);
	self->cursor = LRG_LIST_VIEW_INVALID_POSITION;
	self->anchor = LRG_LIST_VIEW_INVALID_POSITION;
	unbind_from (self, 0);
	lrg_scroll_axis_scroll_to (&self->ay, 0.0f, FALSE);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
	if (had_selection)
		selection_changed (self);
}

GListModel *
lrg_list_view_get_model (LrgListView *self)
{
	g_return_val_if_fail (LRG_IS_LIST_VIEW (self), NULL);
	return self->model;
}

void
lrg_list_view_set_factory (LrgListView           *self,
                           LrgListViewCreateFunc  create,
                           LrgListViewBindFunc    bind,
                           gpointer               user_data,
                           GDestroyNotify         destroy)
{
	g_return_if_fail (LRG_IS_LIST_VIEW (self));

	/* Old rows were made by the old factory: throw them away */
	drop_pool (self);
	clear_factory (self);
	self->create = create;
	self->bind = bind;
	self->user_data = user_data;
	self->destroy = destroy;

	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));
}

gfloat
lrg_list_view_get_row_height (LrgListView *self)
{
	g_return_val_if_fail (LRG_IS_LIST_VIEW (self), 0.0f);
	return self->row_height;
}

void
lrg_list_view_set_row_height (LrgListView *self,
                              gfloat       row_height)
{
	g_return_if_fail (LRG_IS_LIST_VIEW (self));

	row_height = MAX (row_height, 0.0f);
	if (self->row_height == row_height)
		return;
	self->row_height = row_height;
	self->placed_width = -1.0f;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROW_HEIGHT]);
}

LrgSelectionMode
lrg_list_view_get_selection_mode (LrgListView *self)
{
	g_return_val_if_fail (LRG_IS_LIST_VIEW (self), LRG_SELECTION_MODE_NONE);
	return self->mode;
}

void
lrg_list_view_set_selection_mode (LrgListView      *self,
                                  LrgSelectionMode  mode)
{
	g_return_if_fail (LRG_IS_LIST_VIEW (self));

	if (self->mode == mode)
		return;
	self->mode = mode;

	/* Trim the selection to what the new mode allows */
	if (mode == LRG_SELECTION_MODE_NONE && g_hash_table_size (self->selected) > 0)
	{
		g_hash_table_remove_all (self->selected);
		selection_changed (self);
	}
	else if (mode == LRG_SELECTION_MODE_SINGLE && g_hash_table_size (self->selected) > 1)
	{
		guint keep = lrg_list_view_get_selected (self);

		select_only (self, keep);
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTION_MODE]);
}

guint
lrg_list_view_get_selected (LrgListView *self)
{
	GHashTableIter iter;
	gpointer       key;
	guint          best = LRG_LIST_VIEW_INVALID_POSITION;

	g_return_val_if_fail (LRG_IS_LIST_VIEW (self), LRG_LIST_VIEW_INVALID_POSITION);

	if (self->cursor != LRG_LIST_VIEW_INVALID_POSITION &&
	    g_hash_table_contains (self->selected, KEY (self->cursor)))
		return self->cursor;

	g_hash_table_iter_init (&iter, self->selected);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		best = MIN (best, GPOINTER_TO_UINT (key) - 1);
	return best;
}

guint
lrg_list_view_get_n_selected (LrgListView *self)
{
	g_return_val_if_fail (LRG_IS_LIST_VIEW (self), 0);
	return g_hash_table_size (self->selected);
}

gboolean
lrg_list_view_is_selected (LrgListView *self,
                           guint        position)
{
	g_return_val_if_fail (LRG_IS_LIST_VIEW (self), FALSE);
	return g_hash_table_contains (self->selected, KEY (position));
}

void
lrg_list_view_select (LrgListView *self,
                      guint        position)
{
	g_return_if_fail (LRG_IS_LIST_VIEW (self));

	if (position >= n_items (self))
		return;

	if (self->mode == LRG_SELECTION_MODE_SINGLE)
		select_only (self, position);
	else if (self->mode == LRG_SELECTION_MODE_MULTIPLE && !g_hash_table_contains (self->selected, KEY (position)))
	{
		g_hash_table_add (self->selected, KEY (position));
		selection_changed (self);
	}
	self->anchor = position;
	set_cursor (self, position, FALSE);
}

void
lrg_list_view_unselect (LrgListView *self,
                        guint        position)
{
	g_return_if_fail (LRG_IS_LIST_VIEW (self));

	if (g_hash_table_remove (self->selected, KEY (position)))
		selection_changed (self);
}

void
lrg_list_view_unselect_all (LrgListView *self)
{
	g_return_if_fail (LRG_IS_LIST_VIEW (self));

	if (g_hash_table_size (self->selected) == 0)
		return;
	g_hash_table_remove_all (self->selected);
	selection_changed (self);
}

guint
lrg_list_view_get_cursor (LrgListView *self)
{
	g_return_val_if_fail (LRG_IS_LIST_VIEW (self), LRG_LIST_VIEW_INVALID_POSITION);
	return self->cursor;
}

void
lrg_list_view_scroll_to (LrgListView *self,
                         guint        position,
                         gboolean     animate)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;
	gfloat rh;
	gfloat top;
	gfloat target;

	g_return_if_fail (LRG_IS_LIST_VIEW (self));

	if (position >= n_items (self))
		return;

	update_extent (self);
	viewport (self, &pad, &vw, &vh);
	rh = row_h (self);
	top = (gfloat) position * rh;

	/* Smallest move that shows the whole row */
	target = self->ay.bounce ? lrg_scroll_axis_clamp (&self->ay, self->ay.pos.target) : self->ay.pos.target;
	if (top + rh > target + vh)
		target = top + rh - vh;
	if (top < target)
		target = top;

	lrg_scroll_axis_scroll_to (&self->ay, target, animate && smooth (self));
	lrg_scroll_fx_poke (&self->fx);
	if (!(animate && smooth (self)))
		sync_rows (self);
}

gfloat
lrg_list_view_get_scroll_offset (LrgListView *self)
{
	g_return_val_if_fail (LRG_IS_LIST_VIEW (self), 0.0f);
	return lrg_scroll_axis_get_value (&self->ay);
}

LrgWidget *
lrg_list_view_get_row_widget (LrgListView *self,
                              guint        position)
{
	guint i;

	g_return_val_if_fail (LRG_IS_LIST_VIEW (self), NULL);

	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		if (r->position == position)
			return r->widget;
	}
	return NULL;
}

guint
lrg_list_view_get_n_rows (LrgListView *self)
{
	g_return_val_if_fail (LRG_IS_LIST_VIEW (self), 0);
	return self->rows->len;
}
