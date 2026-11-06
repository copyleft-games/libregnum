/* lrg-tree-view.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTreeView: A virtualized expandable tree of rows.
 *
 * The hierarchy is a root GListModel plus a children callback that
 * returns a GListModel for any item (NULL for leaves). The view keeps
 * a flat array of the visible nodes (root items, and the descendants
 * of expanded items in depth-first order) and virtualizes it exactly
 * like LrgListView: only the rows inside the viewport plus a buffer
 * exist as widgets, recycled as the tree scrolls.
 *
 * Per item, a NodeInfo caches the children model (queried once, the
 * first time the item is shown or expanded) and the expansion flag, so
 * collapsing a branch and opening it again restores its sub-branches.
 * Rows are matched to items, not positions: when a branch opens above
 * a row the row keeps its item (and the spring of its disclosure
 * arrow), it only moves.
 *
 * Selection and the cursor are stored as items, so they survive
 * expanding and collapsing. Items must appear only once in the tree.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>

#include "lrg-tree-view.h"
#include "lrg-label.h"
#include "lrg-scrollbar-private.h"
#include "../lrg-log.h"

/* Rows kept beyond each end of the visible range */
#define ROW_BUFFER (1)

/* Two presses on one row within this time (canvas clock) activate it */
#define DOUBLE_CLICK_TIME (0.4)

/* Inset of the row background inside the viewport */
#define ROW_INSET (2.0f)

/* Indentation per level unless set (the theme has no metric for it) */
#define DEFAULT_INDENT (18.0f)

/* Preferred size when nothing else says otherwise */
#define NATURAL_WIDTH (240.0f)
#define NATURAL_ROWS  (10)

typedef struct
{
	GObject    *item;        /* owned */
	GListModel *children;    /* owned, NULL for leaves */
	gulong      changed_id;
	gboolean    queried;     /* children was asked for */
	gboolean    expanded;
} NodeInfo;

typedef struct
{
	GObject  *item;          /* owned */
	guint     depth;
	NodeInfo *info;          /* owned by the info table, may be NULL */
} Flat;

typedef struct
{
	LrgWidget *widget;       /* owned */
	GObject   *item;         /* owned while bound */
	guint      position;     /* flat index, or LRG_TREE_VIEW_INVALID_POSITION */
	guint      depth;
	LrgSpring  arrow;        /* 0 collapsed (pointing right) .. 1 expanded (down) */
	gfloat     dy;           /* row y inside its slot, from the last placement */
	gboolean   placed;
} Row;

struct _LrgTreeView
{
	LrgContainer             parent_instance;

	GListModel              *model;
	gulong                   items_changed_id;

	LrgTreeViewChildrenFunc  children_func;
	gpointer                 children_data;
	GDestroyNotify           children_destroy;

	LrgTreeViewCreateFunc    create;
	LrgTreeViewBindFunc      bind;
	gpointer                 user_data;
	GDestroyNotify           destroy;

	GHashTable              *infos;      /* GObject * -> NodeInfo * */
	GArray                  *flat;       /* Flat */
	GHashTable              *index;      /* GObject * -> flat index + 1 */

	GArray                  *rows;       /* Row */
	gfloat                   row_height;
	gfloat                   indent;
	gfloat                   placed_width;

	LrgSelectionMode         mode;
	GHashTable              *selected;   /* set of GObject * (owned) */
	GObject                 *cursor;     /* owned */

	LrgScrollAxis            ay;
	LrgScrollFx              fx;

	gdouble                  clock;
	gboolean                 click_armed;
	gdouble                  last_click_time;
	GObject                 *last_click_item; /* not owned; compared only */
	gboolean                 in_layout;
	gboolean                 stepped;
};

G_DEFINE_FINAL_TYPE (LrgTreeView, lrg_tree_view, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_MODEL,
	PROP_ROW_HEIGHT,
	PROP_INDENT,
	PROP_SELECTION_MODE,
	N_PROPS
};

enum
{
	SIGNAL_SELECTION_CHANGED,
	SIGNAL_ROW_ACTIVATED,
	SIGNAL_ROW_EXPANDED,
	SIGNAL_ROW_COLLAPSED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

static void rebuild_flat (LrgTreeView *self);

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static guint
n_visible (LrgTreeView *self)
{
	return self->flat->len;
}

static Flat *
flat_at (LrgTreeView *self,
         guint        p)
{
	return &g_array_index (self->flat, Flat, p);
}

static gfloat
row_h (LrgTreeView *self)
{
	gfloat h = self->row_height;

	if (h <= 0.0f)
		h = lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_METRIC_ROW_HEIGHT);
	return MAX (h, 1.0f);
}

static void
viewport (LrgTreeView *self,
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
smooth (LrgTreeView *self)
{
	return lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;
}

static guint
rows_per_page (LrgTreeView *self)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;

	viewport (self, &pad, &vw, &vh);
	return MAX (1u, (guint) floorf (vh / row_h (self)));
}

static void
update_extent (LrgTreeView *self)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;

	viewport (self, &pad, &vw, &vh);
	lrg_scroll_axis_set_extent (&self->ay, (gfloat) n_visible (self) * row_h (self), vh);
}

/* position_of: flat index of @item, or invalid */
static guint
position_of (LrgTreeView *self,
             GObject     *item)
{
	gpointer v;

	if (item == NULL)
		return LRG_TREE_VIEW_INVALID_POSITION;
	v = g_hash_table_lookup (self->index, item);
	return v != NULL ? GPOINTER_TO_UINT (v) - 1 : LRG_TREE_VIEW_INVALID_POSITION;
}

/* ==========================================================================
 * Node infos: cached children and expansion
 * ========================================================================== */

static void
node_info_free (gpointer data)
{
	NodeInfo *info = data;

	if (info->children != NULL)
	{
		g_clear_signal_handler (&info->changed_id, info->children);
		g_clear_object (&info->children);
	}
	g_clear_object (&info->item);
	g_free (info);
}

static void
on_children_changed (GListModel  *model,
                     guint        position,
                     guint        removed,
                     guint        added,
                     LrgTreeView *self)
{
	rebuild_flat (self);
}

/* get_info: the NodeInfo of @item, created on demand */
static NodeInfo *
get_info (LrgTreeView *self,
          GObject     *item)
{
	NodeInfo *info = g_hash_table_lookup (self->infos, item);

	if (info == NULL)
	{
		info = g_new0 (NodeInfo, 1);
		info->item = g_object_ref (item);
		g_hash_table_insert (self->infos, item, info);
	}
	return info;
}

/*
 * query_children:
 *
 * Asks the children callback once and caches the result. Returns the
 * children model, or NULL for a leaf.
 */
static GListModel *
query_children (LrgTreeView *self,
                NodeInfo    *info)
{
	if (info->queried)
		return info->children;
	info->queried = TRUE;

	if (self->children_func != NULL)
		info->children = self->children_func (self, info->item, self->children_data);
	if (info->children != NULL && g_list_model_get_n_items (info->children) == 0 &&
	    !G_IS_LIST_STORE (info->children))
		g_clear_object (&info->children);
	if (info->children != NULL)
		info->changed_id = g_signal_connect (info->children, "items-changed",
		                                     G_CALLBACK (on_children_changed), self);
	return info->children;
}

/* has_children: does @item show a disclosure arrow? */
static gboolean
has_children (LrgTreeView *self,
              GObject     *item)
{
	GListModel *children = query_children (self, get_info (self, item));

	return children != NULL && g_list_model_get_n_items (children) > 0;
}

/* ==========================================================================
 * Flattening
 * ========================================================================== */

/* flatten_into: append the items of @model and expanded descendants */
static void
flatten_into (LrgTreeView *self,
              GListModel  *model,
              guint        depth)
{
	guint n = g_list_model_get_n_items (model);
	guint i;

	for (i = 0; i < n; i++)
	{
		Flat f;

		f.item = g_list_model_get_item (model, i);
		if (f.item == NULL)
			continue;
		f.depth = depth;
		f.info = g_hash_table_lookup (self->infos, f.item);
		g_array_append_val (self->flat, f);
		g_hash_table_insert (self->index, f.item, GUINT_TO_POINTER (self->flat->len));

		/* Depth guard against cyclic hierarchies */
		if (f.info != NULL && f.info->expanded && depth < 256)
		{
			GListModel *children = query_children (self, f.info);

			if (children != NULL)
				flatten_into (self, children, depth + 1);
		}
	}
}

static void
clear_flat (LrgTreeView *self)
{
	guint i;

	for (i = 0; i < self->flat->len; i++)
		g_clear_object (&g_array_index (self->flat, Flat, i).item);
	g_array_set_size (self->flat, 0);
	g_hash_table_remove_all (self->index);
}

/*
 * rebuild_flat:
 *
 * Recomputes the visible rows. Selected items that are no longer
 * visible stay selected (they reappear selected); the cursor moves to
 * its nearest visible ancestor in the caller when needed.
 */
static void
rebuild_flat (LrgTreeView *self)
{
	clear_flat (self);
	if (self->model != NULL)
		flatten_into (self, self->model, 0);

	update_extent (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

/* ==========================================================================
 * Default factory
 * ========================================================================== */

static LrgWidget *
default_create (LrgTreeView *self,
                gpointer     user_data)
{
	LrgLabel *label = lrg_label_new ("");

	lrg_widget_set_valign (LRG_WIDGET (label), LRG_ALIGN_CENTER);
	return LRG_WIDGET (label);
}

static void
default_bind (LrgTreeView *self,
              LrgWidget   *row,
              GObject     *item,
              guint        depth,
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

static void
grow_pool (LrgTreeView *self,
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
		LrgWidget *w = self->create != NULL ? self->create (self, self->user_data) : default_create (self, NULL);

		if (w == NULL)
		{
			g_warning ("LrgTreeView: row factory returned NULL");
			break;
		}
		row.widget = w;
		row.item = NULL;
		row.position = LRG_TREE_VIEW_INVALID_POSITION;
		row.depth = 0;
		row.placed = FALSE;
		row.dy = 0.0f;
		lrg_spring_init (&row.arrow, 0.0f, LRG_MOTION_SMOOTH);
		lrg_widget_set_visible (w, FALSE);
		lrg_container_add_child (c, w);
		g_array_append_val (self->rows, row);
	}
	lrg_container_add_child (c, self->ay.bar);
	self->in_layout = was;
}

static void
drop_pool (LrgTreeView *self)
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
		g_clear_object (&r->item);
	}
	g_array_set_size (self->rows, 0);
	self->in_layout = was;
}

/* unbind_all: forget every binding (model or factory changed) */
static void
unbind_all (LrgTreeView *self)
{
	guint i;

	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		r->position = LRG_TREE_VIEW_INVALID_POSITION;
		g_clear_object (&r->item);
	}
}

/* bind_row: show flat row @p in @r; a new item snaps its arrow */
static void
bind_row (LrgTreeView *self,
          Row         *r,
          guint        p)
{
	Flat     *f = flat_at (self, p);
	NodeInfo *info = get_info (self, f->item);

	g_set_object (&r->item, f->item);
	r->position = p;
	r->depth = f->depth;
	r->placed = FALSE;
	lrg_spring_snap (&r->arrow, info->expanded ? 1.0f : 0.0f);
	query_children (self, info);

	if (self->bind != NULL)
		self->bind (self, r->widget, f->item, f->depth, self->user_data);
	else
		default_bind (self, r->widget, f->item, f->depth, NULL);
}

/* row_slot_x: left edge of the row widget's slot at @depth (relative) */
static gfloat
row_slot_x (LrgTreeView *self,
            guint        depth)
{
	gfloat pad = lrg_container_get_padding (LRG_CONTAINER (self));

	/* indentation, then one indent-wide column for the arrow */
	return pad + ROW_INSET + (gfloat) depth * self->indent + self->indent;
}

/*
 * sync_rows:
 *
 * Virtualization. Rows already showing an item inside the range keep
 * it (moving to the item's new position), the rest are rebound to the
 * positions nobody shows, leftovers are hidden.
 */
static void
sync_rows (LrgTreeView *self)
{
	guint  n = n_visible (self);
	gfloat rh = row_h (self);
	gfloat pad;
	gfloat vw;
	gfloat vh;
	gfloat off;
	guint  first;
	guint  start;
	guint  end;
	guint  p;
	guint  i;

	viewport (self, &pad, &vw, &vh);
	off = lrg_scroll_axis_get_value (&self->ay);

	/* 1. Range */
	first = off > 0.0f ? (guint) floorf (off / rh) : 0;
	start = first > ROW_BUFFER ? first - ROW_BUFFER : 0;
	end = MIN (n, first + (guint) ceilf (vh / rh) + ROW_BUFFER);
	if (start > end)
		start = end;

	if (self->rows->len < end - start)
		grow_pool (self, (end - start) - self->rows->len);

	/* 2. Follow items to their current positions; free the others */
	for (i = 0; i < self->rows->len; i++)
	{
		Row  *r = &g_array_index (self->rows, Row, i);
		guint at = position_of (self, r->item);

		if (at != LRG_TREE_VIEW_INVALID_POSITION && at >= start && at < end)
		{
			if (at != r->position || flat_at (self, at)->depth != r->depth)
				r->placed = FALSE;
			r->position = at;
			r->depth = flat_at (self, at)->depth;
		}
		else
		{
			r->position = LRG_TREE_VIEW_INVALID_POSITION;
			g_clear_object (&r->item);
		}
	}

	/* 3. Bind free rows to positions nobody shows */
	for (p = start; p < end; p++)
	{
		Row     *free_row = NULL;
		gboolean shown = FALSE;

		for (i = 0; i < self->rows->len && !shown; i++)
		{
			Row *r = &g_array_index (self->rows, Row, i);

			if (r->position == p)
				shown = TRUE;
			else if (free_row == NULL && r->position == LRG_TREE_VIEW_INVALID_POSITION)
				free_row = r;
		}
		if (!shown && free_row != NULL)
			bind_row (self, free_row, p);
	}

	/* 4. Place */
	if (vw != self->placed_width)
	{
		self->placed_width = vw;
		for (i = 0; i < self->rows->len; i++)
			g_array_index (self->rows, Row, i).placed = FALSE;
	}
	for (i = 0; i < self->rows->len; i++)
	{
		Row   *r = &g_array_index (self->rows, Row, i);
		gfloat sx;
		gfloat sy;

		if (r->position == LRG_TREE_VIEW_INVALID_POSITION)
		{
			lrg_widget_set_visible (r->widget, FALSE);
			continue;
		}
		lrg_widget_set_visible (r->widget, TRUE);
		sx = row_slot_x (self, r->depth);
		sy = pad + (gfloat) r->position * rh - off;
		if (!r->placed)
		{
			lrg_widget_place_in_slot (r->widget, sx, sy, MAX (0.0f, pad + vw - ROW_INSET - sx), rh);
			r->dy = lrg_widget_get_y (r->widget) - sy;
			r->placed = TRUE;
		}
		else
			lrg_widget_set_y (r->widget, sy + r->dy);

		if (g_hash_table_contains (self->selected, r->item))
			lrg_widget_set_state_flags (r->widget, LRG_WIDGET_STATE_SELECTED);
		else
			lrg_widget_unset_state_flags (r->widget, LRG_WIDGET_STATE_SELECTED);
	}
}

/* ==========================================================================
 * Selection and cursor
 * ========================================================================== */

static void
selection_changed (LrgTreeView *self)
{
	g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

static void
select_only (LrgTreeView *self,
             GObject     *item)
{
	if (self->mode == LRG_SELECTION_MODE_NONE)
		return;
	if (g_hash_table_size (self->selected) == 1 && g_hash_table_contains (self->selected, item))
		return;
	g_hash_table_remove_all (self->selected);
	g_hash_table_add (self->selected, g_object_ref (item));
	selection_changed (self);
}

/* move_cursor_to: keyboard movement to flat row @p; selection follows */
static void
move_cursor_to (LrgTreeView *self,
                guint        p)
{
	guint n = n_visible (self);

	if (n == 0)
		return;
	p = MIN (p, n - 1);
	g_set_object (&self->cursor, flat_at (self, p)->item);
	select_only (self, self->cursor);
	lrg_tree_view_scroll_to (self, p, TRUE);
}

/* cursor_pos: flat row of the cursor, or invalid */
static guint
cursor_pos (LrgTreeView *self)
{
	return position_of (self, self->cursor);
}

/* ==========================================================================
 * Expansion
 * ========================================================================== */

/*
 * set_expanded:
 *
 * Flips one item, re-flattens, turns the arrow of its realized row
 * with the spring, and moves a cursor that was hidden inside a
 * collapsed branch to the collapsed item.
 */
static void
set_expanded (LrgTreeView *self,
              GObject     *item,
              gboolean     expanded)
{
	NodeInfo *info;
	guint     i;

	g_return_if_fail (G_IS_OBJECT (item));

	info = get_info (self, item);
	if (info->expanded == expanded)
		return;
	if (expanded && !has_children (self, item))
		return;
	info->expanded = expanded;

	/* Re-point flat entries at the (possibly new) info before flattening */
	rebuild_flat (self);

	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		if (r->item == item)
		{
			lrg_spring_set_target (&r->arrow, expanded ? 1.0f : 0.0f);
			if (!smooth (self))
				lrg_spring_snap (&r->arrow, r->arrow.target);
		}
	}

	if (!expanded && self->cursor != NULL && position_of (self, self->cursor) == LRG_TREE_VIEW_INVALID_POSITION)
		g_set_object (&self->cursor, item);

	sync_rows (self);
	g_signal_emit (self, signals[expanded ? SIGNAL_ROW_EXPANDED : SIGNAL_ROW_COLLAPSED], 0, item);
}

/* ==========================================================================
 * Model
 * ========================================================================== */

static void
on_items_changed (GListModel  *model,
                  guint        position,
                  guint        removed,
                  guint        added,
                  LrgTreeView *self)
{
	rebuild_flat (self);
	sync_rows (self);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_tree_view_layout_children (LrgContainer *container)
{
	LrgTreeView *self = LRG_TREE_VIEW (container);
	gfloat       pad;
	gfloat       vw;
	gfloat       vh;
	gfloat       zone;

	if (self->in_layout || self->ay.bar == NULL)
		return;
	self->in_layout = TRUE;

	viewport (self, &pad, &vw, &vh);
	update_extent (self);
	zone = lrg_scroll_bar_zone (LRG_WIDGET (self));
	lrg_widget_set_visible (self->ay.bar, lrg_scroll_axis_get_max (&self->ay) > 0.0f);
	lrg_widget_allocate (self->ay.bar, pad + vw - zone, pad, zone, vh);

	sync_rows (self);
	self->in_layout = FALSE;
}

static void
lrg_tree_view_measure (LrgWidget *widget,
                       gfloat    *preferred_width,
                       gfloat    *preferred_height)
{
	LrgTreeView *self = LRG_TREE_VIEW (widget);
	gfloat       pad = lrg_container_get_padding (LRG_CONTAINER (self));
	guint        rows = MIN (n_visible (self), NATURAL_ROWS);

	*preferred_width = NATURAL_WIDTH + pad * 2.0f;
	*preferred_height = (gfloat) MAX (rows, 1u) * row_h (self) + pad * 2.0f;
}

/* ==========================================================================
 * Looks and painting
 * ========================================================================== */

static void
lrg_tree_view_style_defaults (LrgWidget      *widget,
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

	case LRG_UI_PART_ARROW:
		/* Disclosure chevron: muted, TEXT under the pointer, accent when open */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & LRG_WIDGET_STATE_OPEN) ? LRG_THEME_COLOR_ACCENT :
		                              (state & LRG_WIDGET_STATE_HOVERED) ? LRG_THEME_COLOR_TEXT
		                                                                 : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE) * 0.45f);
		return;

	case LRG_UI_PART_INDICATOR:
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_FOCUS_RING_WIDTH) * 0.75f);
		lrg_part_look_set_radius (look, MAX (2.0f, radius - ROW_INSET));
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_tree_view_parent_class)->style_defaults (widget, part, state, look);
}

/* paint_arrow: chevron at (@cx, @cy) pointing right, turned by @turn quarter turns */
static void
paint_arrow (LrgPaintList   *list,
             gfloat          cx,
             gfloat          cy,
             gfloat          s,
             gfloat          turn,
             const GrlColor *color)
{
	gfloat angle = turn * (gfloat) G_PI * 0.5f;
	gfloat ca = cosf (angle);
	gfloat sa = sinf (angle);
	gfloat px[3] = { -s * 0.25f, s * 0.25f, -s * 0.25f };
	gfloat py[3] = { -s * 0.5f, 0.0f, s * 0.5f };
	gfloat rx[3];
	gfloat ry[3];
	gfloat t = MAX (1.5f, s * 0.22f);
	guint  k;

	for (k = 0; k < 3; k++)
	{
		rx[k] = cx + px[k] * ca - py[k] * sa;
		ry[k] = cy + px[k] * sa + py[k] * ca;
	}
	lrg_paint_list_line (list, rx[0], ry[0], rx[1], ry[1], t, color);
	lrg_paint_list_line (list, rx[1], ry[1], rx[2], ry[2], t, color);
}

static void
paint_row (LrgTreeView  *self,
           LrgPaintList *list,
           Row          *r,
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

	if (g_hash_table_contains (self->selected, r->item))
		base |= LRG_WIDGET_STATE_SELECTED;

	/* 1. Background blended by the row's own springs */
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

	/* 2. Disclosure arrow for items with children */
	if (r->item != NULL && has_children (self, r->item))
	{
		LrgWidgetState as = base;
		gfloat         ax = lrg_widget_get_world_x (widget) + row_slot_x (self, r->depth) - self->indent * 0.5f;

		if (get_info (self, r->item)->expanded)
			as |= LRG_WIDGET_STATE_OPEN;
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ARROW, as, &look);
		if (hover > 0.001f)
		{
			lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_ARROW, as | LRG_WIDGET_STATE_HOVERED, &variant);
			lrg_part_look_lerp (&look, &variant, hover, &look);
		}
		paint_arrow (list, ax, y + h * 0.5f, look.size > 0.0f ? look.size : 7.0f,
		             CLAMP (r->arrow.value, -0.2f, 1.2f), &look.foreground);
	}

	/* 3. Cursor outline after keyboard navigation */
	if (r->item != NULL && r->item == self->cursor &&
	    (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_FOCUS_VISIBLE))
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_INDICATOR, base, &variant);
		lrg_paint_list_rect_ex (list, x, y, w, h, &variant.radii, NULL, NULL, variant.border_width,
		                        &variant.border);
	}
}

/*
 * lrg_tree_view_step:
 *
 * Canvas clock, scroll spring and fade, and the arrow springs of the
 * realized rows (snapped on the first frame).
 */
static gboolean
lrg_tree_view_step (LrgWidget *widget,
                    gfloat     delta_time)
{
	LrgTreeView *self = LRG_TREE_VIEW (widget);
	LrgMotion    motion = lrg_widget_get_motion (widget);
	gboolean     moving;
	gboolean     keep;
	guint        i;

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;
	if (self->ay.bar == NULL)
		return FALSE;

	moving = lrg_scroll_axis_step_snap (&self->ay, delta_time);
	if (moving)
		lrg_scroll_fx_poke (&self->fx);
	sync_rows (self);

	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		lrg_spring_set_motion (&r->arrow, motion);
		if (!self->stepped || motion == LRG_MOTION_NONE)
			lrg_spring_snap (&r->arrow, r->arrow.target);
		else
			moving |= lrg_spring_step (&r->arrow, delta_time);
	}
	self->stepped = TRUE;

	keep = self->ay.dragging || (lrg_widget_get_state_flags (self->ay.bar) & LRG_WIDGET_STATE_HOVERED);
	return lrg_scroll_fx_update (&self->fx, keep, delta_time) || moving;
}

static void
lrg_tree_view_paint (LrgWidget    *widget,
                     LrgPaintList *list)
{
	LrgTreeView *self = LRG_TREE_VIEW (widget);
	LrgPartLook  box;
	gfloat       wx = lrg_widget_get_world_x (widget);
	gfloat       wy = lrg_widget_get_world_y (widget);
	gfloat       rh = row_h (self);
	gfloat       pad;
	gfloat       vw;
	gfloat       vh;
	gfloat       off;
	guint        i;

	sync_rows (self);

	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, wx, wy, lrg_widget_get_width (widget), lrg_widget_get_height (widget));

	viewport (self, &pad, &vw, &vh);
	off = lrg_scroll_axis_get_value (&self->ay);
	lrg_paint_list_push_clip (list, wx + pad, wy + pad, vw, vh);
	for (i = 0; i < self->rows->len; i++)
	{
		Row   *r = &g_array_index (self->rows, Row, i);
		gfloat ry;

		if (r->position == LRG_TREE_VIEW_INVALID_POSITION)
			continue;
		ry = wy + pad + (gfloat) r->position * rh - off;
		paint_row (self, list, r, wx + pad + ROW_INSET, ry + 1.0f, MAX (0.0f, vw - ROW_INSET * 2.0f),
		           MAX (0.0f, rh - 2.0f));
		lrg_widget_paint (r->widget, list);
	}
	lrg_paint_list_pop_clip (list);

	lrg_scroll_bar_paint (widget, list, &self->ay, TRUE, self->fx.fade.value);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

static guint
position_at (LrgTreeView *self,
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
		return LRG_TREE_VIEW_INVALID_POSITION;
	rel += lrg_scroll_axis_get_value (&self->ay);
	if (rel < 0.0f)
		return LRG_TREE_VIEW_INVALID_POSITION;
	p = (guint) floorf (rel / row_h (self));
	return p < n_visible (self) ? p : LRG_TREE_VIEW_INVALID_POSITION;
}

/*
 * click_row:
 *
 * A press on the arrow column of an item with children toggles it.
 * Anywhere else selects (Ctrl toggles in multiple mode); a second
 * press on the same item within DOUBLE_CLICK_TIME activates it.
 */
static void
click_row (LrgTreeView       *self,
           guint              p,
           gfloat             x,
           LrgInputModifiers  mods)
{
	Flat   *f = flat_at (self, p);
	GObject *item = f->item;
	gfloat  ax = lrg_widget_get_world_x (LRG_WIDGET (self)) + row_slot_x (self, f->depth) - self->indent;

	g_set_object (&self->cursor, item);

	if (x >= ax && x < ax + self->indent && has_children (self, item))
	{
		set_expanded (self, item, !get_info (self, item)->expanded);
		self->click_armed = FALSE;
		return;
	}

	if (self->mode == LRG_SELECTION_MODE_MULTIPLE && (mods & LRG_INPUT_MODIFIER_CTRL))
	{
		if (!g_hash_table_remove (self->selected, item))
			g_hash_table_add (self->selected, g_object_ref (item));
		selection_changed (self);
	}
	else
		select_only (self, item);

	if (self->click_armed && self->last_click_item == item &&
	    self->clock - self->last_click_time <= DOUBLE_CLICK_TIME)
	{
		self->click_armed = FALSE;
		g_signal_emit (self, signals[SIGNAL_ROW_ACTIVATED], 0, item);
		return;
	}
	self->click_armed = TRUE;
	self->last_click_time = self->clock;
	self->last_click_item = item;
}

/*
 * handle_key:
 *
 * Up / Down / Page / Home / End move the cursor. Right expands a
 * collapsed parent, or steps into an expanded one; Left collapses an
 * expanded item, or goes to the parent.
 */
static gboolean
handle_key (LrgTreeView *self,
            GrlKey       key)
{
	guint    n = n_visible (self);
	guint    cur = cursor_pos (self);
	guint    page = rows_per_page (self);
	gboolean none = cur == LRG_TREE_VIEW_INVALID_POSITION;

	switch (key)
	{
	case GRL_KEY_UP:
		if (n > 0)
			move_cursor_to (self, none ? 0 : (cur > 0 ? cur - 1 : 0));
		return TRUE;
	case GRL_KEY_DOWN:
		if (n > 0)
			move_cursor_to (self, none ? 0 : cur + 1);
		return TRUE;
	case GRL_KEY_HOME:
		if (n > 0)
			move_cursor_to (self, 0);
		return TRUE;
	case GRL_KEY_END:
		if (n > 0)
			move_cursor_to (self, n - 1);
		return TRUE;
	case GRL_KEY_PAGE_UP:
		if (n > 0)
			move_cursor_to (self, none ? 0 : (cur > page ? cur - page : 0));
		return TRUE;
	case GRL_KEY_PAGE_DOWN:
		if (n > 0)
			move_cursor_to (self, none ? MIN (page, n - 1) : cur + page);
		return TRUE;

	case GRL_KEY_RIGHT:
		if (none)
		{
			if (n > 0)
				move_cursor_to (self, 0);
			return TRUE;
		}
		if (has_children (self, self->cursor))
		{
			if (!get_info (self, self->cursor)->expanded)
				set_expanded (self, self->cursor, TRUE);
			else
				move_cursor_to (self, cur + 1);
		}
		return TRUE;

	case GRL_KEY_LEFT:
		if (none)
			return TRUE;
		if (get_info (self, self->cursor)->expanded)
			set_expanded (self, self->cursor, FALSE);
		else
		{
			/* The parent is the closest row above with a smaller depth */
			guint depth = flat_at (self, cur)->depth;
			guint p = cur;

			while (depth > 0 && p > 0)
			{
				p--;
				if (flat_at (self, p)->depth < depth)
				{
					move_cursor_to (self, p);
					break;
				}
			}
		}
		return TRUE;

	default:
		return FALSE;
	}
}

static gboolean
lrg_tree_view_handle_event (LrgWidget        *widget,
                            const LrgUIEvent *event)
{
	LrgTreeView *self = LRG_TREE_VIEW (widget);
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
			if (p == LRG_TREE_VIEW_INVALID_POSITION)
				return FALSE;
			click_row (self, p, lrg_ui_event_get_x (event), lrg_ui_event_get_modifiers (event));
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
		return handle_key (self, lrg_ui_event_get_key (event));

	case LRG_UI_EVENT_ACTIVATE:
		if (self->cursor == NULL || cursor_pos (self) == LRG_TREE_VIEW_INVALID_POSITION)
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
clear_factory (LrgTreeView *self)
{
	if (self->destroy != NULL && self->user_data != NULL)
		self->destroy (self->user_data);
	self->create = NULL;
	self->bind = NULL;
	self->user_data = NULL;
	self->destroy = NULL;
}

static void
clear_children_func (LrgTreeView *self)
{
	if (self->children_destroy != NULL && self->children_data != NULL)
		self->children_destroy (self->children_data);
	self->children_func = NULL;
	self->children_data = NULL;
	self->children_destroy = NULL;
}

static void
lrg_tree_view_constructed (GObject *object)
{
	LrgTreeView *self = LRG_TREE_VIEW (object);

	G_OBJECT_CLASS (lrg_tree_view_parent_class)->constructed (object);

	self->in_layout = TRUE;
	self->ay.bar = lrg_scroll_bar_new ("vscrollbar");
	lrg_container_add_child (LRG_CONTAINER (self), self->ay.bar);
	self->in_layout = FALSE;
}

static void
lrg_tree_view_dispose (GObject *object)
{
	LrgTreeView *self = LRG_TREE_VIEW (object);

	if (self->model != NULL)
	{
		g_clear_signal_handler (&self->items_changed_id, self->model);
		g_clear_object (&self->model);
	}
	if (self->rows != NULL)
		drop_pool (self);
	if (self->flat != NULL)
		clear_flat (self);
	if (self->infos != NULL)
		g_hash_table_remove_all (self->infos);
	if (self->selected != NULL)
		g_hash_table_remove_all (self->selected);
	g_clear_object (&self->cursor);
	clear_factory (self);
	clear_children_func (self);
	g_clear_object (&self->ay.bar);

	G_OBJECT_CLASS (lrg_tree_view_parent_class)->dispose (object);
}

static void
lrg_tree_view_finalize (GObject *object)
{
	LrgTreeView *self = LRG_TREE_VIEW (object);

	g_clear_pointer (&self->rows, g_array_unref);
	g_clear_pointer (&self->flat, g_array_unref);
	g_clear_pointer (&self->index, g_hash_table_unref);
	g_clear_pointer (&self->infos, g_hash_table_unref);
	g_clear_pointer (&self->selected, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_tree_view_parent_class)->finalize (object);
}

static void
lrg_tree_view_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
	LrgTreeView *self = LRG_TREE_VIEW (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		g_value_set_object (value, self->model);
		break;
	case PROP_ROW_HEIGHT:
		g_value_set_float (value, self->row_height);
		break;
	case PROP_INDENT:
		g_value_set_float (value, self->indent);
		break;
	case PROP_SELECTION_MODE:
		g_value_set_enum (value, self->mode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_tree_view_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
	LrgTreeView *self = LRG_TREE_VIEW (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		lrg_tree_view_set_model (self, g_value_get_object (value));
		break;
	case PROP_ROW_HEIGHT:
		lrg_tree_view_set_row_height (self, g_value_get_float (value));
		break;
	case PROP_INDENT:
		lrg_tree_view_set_indent (self, g_value_get_float (value));
		break;
	case PROP_SELECTION_MODE:
		lrg_tree_view_set_selection_mode (self, (LrgSelectionMode) g_value_get_enum (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_tree_view_class_init (LrgTreeViewClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->constructed = lrg_tree_view_constructed;
	object_class->dispose = lrg_tree_view_dispose;
	object_class->finalize = lrg_tree_view_finalize;
	object_class->get_property = lrg_tree_view_get_property;
	object_class->set_property = lrg_tree_view_set_property;

	widget_class->measure = lrg_tree_view_measure;
	widget_class->paint = lrg_tree_view_paint;
	widget_class->step = lrg_tree_view_step;
	widget_class->style_defaults = lrg_tree_view_style_defaults;
	widget_class->handle_event = lrg_tree_view_handle_event;

	container_class->layout_children = lrg_tree_view_layout_children;

	/**
	 * LrgTreeView:model:
	 *
	 * The root items.
	 */
	properties[PROP_MODEL] =
		g_param_spec_object ("model", "Model", "The root items",
		                     G_TYPE_LIST_MODEL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTreeView:row-height:
	 *
	 * Height of every row; 0 uses the theme's ROW_HEIGHT metric.
	 */
	properties[PROP_ROW_HEIGHT] =
		g_param_spec_float ("row-height", "Row height", "Height of every row (0 = theme)",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTreeView:indent:
	 *
	 * Horizontal indentation per nesting level; the disclosure arrow
	 * takes one more indent before the row widget.
	 */
	properties[PROP_INDENT] =
		g_param_spec_float ("indent", "Indent", "Indentation per level",
		                    0.0f, G_MAXFLOAT, DEFAULT_INDENT,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTreeView:selection-mode:
	 *
	 * How many items can be selected.
	 */
	properties[PROP_SELECTION_MODE] =
		g_param_spec_enum ("selection-mode", "Selection mode", "How many items can be selected",
		                   LRG_TYPE_SELECTION_MODE, LRG_SELECTION_MODE_SINGLE,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgTreeView::selection-changed:
	 * @self: the tree view
	 *
	 * Emitted when items were selected or unselected.
	 */
	signals[SIGNAL_SELECTION_CHANGED] =
		g_signal_new ("selection-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 0);

	/**
	 * LrgTreeView::row-activated:
	 * @self: the tree view
	 * @item: the activated item
	 *
	 * Emitted on a double click, or Enter / Space / gamepad A on the
	 * cursor row.
	 */
	signals[SIGNAL_ROW_ACTIVATED] =
		g_signal_new ("row-activated",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_OBJECT);

	/**
	 * LrgTreeView::row-expanded:
	 * @self: the tree view
	 * @item: the expanded item
	 *
	 * Emitted after an item was expanded.
	 */
	signals[SIGNAL_ROW_EXPANDED] =
		g_signal_new ("row-expanded",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_OBJECT);

	/**
	 * LrgTreeView::row-collapsed:
	 * @self: the tree view
	 * @item: the collapsed item
	 *
	 * Emitted after an item was collapsed.
	 */
	signals[SIGNAL_ROW_COLLAPSED] =
		g_signal_new ("row-collapsed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 1, G_TYPE_OBJECT);
}

static void
lrg_tree_view_init (LrgTreeView *self)
{
	self->infos = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, node_info_free);
	self->flat = g_array_new (FALSE, TRUE, sizeof (Flat));
	self->index = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->rows = g_array_new (FALSE, TRUE, sizeof (Row));
	self->selected = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, NULL);
	self->mode = LRG_SELECTION_MODE_SINGLE;
	self->indent = DEFAULT_INDENT;
	self->placed_width = -1.0f;
	lrg_scroll_axis_init (&self->ay);
	lrg_scroll_fx_init (&self->fx);

	lrg_container_set_padding (LRG_CONTAINER (self), 4.0f);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTreeView *
lrg_tree_view_new (void)
{
	return g_object_new (LRG_TYPE_TREE_VIEW, NULL);
}

/* reset_tree: drop caches, selection and bindings, then re-flatten */
static void
reset_tree (LrgTreeView *self)
{
	gboolean had_selection = g_hash_table_size (self->selected) > 0;

	unbind_all (self);
	clear_flat (self);
	g_hash_table_remove_all (self->infos);
	g_hash_table_remove_all (self->selected);
	g_clear_object (&self->cursor);
	self->last_click_item = NULL;
	self->click_armed = FALSE;
	lrg_scroll_axis_scroll_to (&self->ay, 0.0f, FALSE);
	rebuild_flat (self);
	lrg_container_layout_children (LRG_CONTAINER (self));
	if (had_selection)
		selection_changed (self);
}

void
lrg_tree_view_set_model (LrgTreeView *self,
                         GListModel  *model)
{
	g_return_if_fail (LRG_IS_TREE_VIEW (self));
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
		self->items_changed_id = g_signal_connect (model, "items-changed", G_CALLBACK (on_items_changed), self);
	}
	reset_tree (self);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}

GListModel *
lrg_tree_view_get_model (LrgTreeView *self)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), NULL);
	return self->model;
}

void
lrg_tree_view_set_children_func (LrgTreeView             *self,
                                 LrgTreeViewChildrenFunc  func,
                                 gpointer                 user_data,
                                 GDestroyNotify           destroy)
{
	g_return_if_fail (LRG_IS_TREE_VIEW (self));

	clear_children_func (self);
	self->children_func = func;
	self->children_data = user_data;
	self->children_destroy = destroy;
	reset_tree (self);
}

void
lrg_tree_view_set_factory (LrgTreeView           *self,
                           LrgTreeViewCreateFunc  create,
                           LrgTreeViewBindFunc    bind,
                           gpointer               user_data,
                           GDestroyNotify         destroy)
{
	g_return_if_fail (LRG_IS_TREE_VIEW (self));

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
lrg_tree_view_get_row_height (LrgTreeView *self)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), 0.0f);
	return self->row_height;
}

void
lrg_tree_view_set_row_height (LrgTreeView *self,
                              gfloat       row_height)
{
	g_return_if_fail (LRG_IS_TREE_VIEW (self));

	row_height = MAX (row_height, 0.0f);
	if (self->row_height == row_height)
		return;
	self->row_height = row_height;
	self->placed_width = -1.0f;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROW_HEIGHT]);
}

gfloat
lrg_tree_view_get_indent (LrgTreeView *self)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), 0.0f);
	return self->indent;
}

void
lrg_tree_view_set_indent (LrgTreeView *self,
                          gfloat       indent)
{
	g_return_if_fail (LRG_IS_TREE_VIEW (self));

	indent = MAX (indent, 0.0f);
	if (self->indent == indent)
		return;
	self->indent = indent;
	self->placed_width = -1.0f;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INDENT]);
}

LrgSelectionMode
lrg_tree_view_get_selection_mode (LrgTreeView *self)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), LRG_SELECTION_MODE_NONE);
	return self->mode;
}

void
lrg_tree_view_set_selection_mode (LrgTreeView      *self,
                                  LrgSelectionMode  mode)
{
	g_return_if_fail (LRG_IS_TREE_VIEW (self));

	if (self->mode == mode)
		return;
	self->mode = mode;
	if (mode == LRG_SELECTION_MODE_NONE && g_hash_table_size (self->selected) > 0)
	{
		g_hash_table_remove_all (self->selected);
		selection_changed (self);
	}
	else if (mode == LRG_SELECTION_MODE_SINGLE && g_hash_table_size (self->selected) > 1)
	{
		GObject *keep = lrg_tree_view_get_selected_item (self);

		g_object_ref (keep);
		g_hash_table_remove_all (self->selected);
		g_hash_table_add (self->selected, keep);
		selection_changed (self);
	}
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTION_MODE]);
}

void
lrg_tree_view_expand (LrgTreeView *self,
                      GObject     *item)
{
	g_return_if_fail (LRG_IS_TREE_VIEW (self));
	set_expanded (self, item, TRUE);
}

void
lrg_tree_view_collapse (LrgTreeView *self,
                        GObject     *item)
{
	g_return_if_fail (LRG_IS_TREE_VIEW (self));
	set_expanded (self, item, FALSE);
}

gboolean
lrg_tree_view_is_expanded (LrgTreeView *self,
                           GObject     *item)
{
	NodeInfo *info;

	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), FALSE);
	info = g_hash_table_lookup (self->infos, item);
	return info != NULL && info->expanded;
}

void
lrg_tree_view_collapse_all (LrgTreeView *self)
{
	GHashTableIter iter;
	gpointer       value;

	g_return_if_fail (LRG_IS_TREE_VIEW (self));

	g_hash_table_iter_init (&iter, self->infos);
	while (g_hash_table_iter_next (&iter, NULL, &value))
		((NodeInfo *) value)->expanded = FALSE;
	rebuild_flat (self);
	if (self->cursor != NULL && position_of (self, self->cursor) == LRG_TREE_VIEW_INVALID_POSITION)
		g_clear_object (&self->cursor);
	sync_rows (self);
}

guint
lrg_tree_view_get_n_visible (LrgTreeView *self)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), 0);
	return n_visible (self);
}

GObject *
lrg_tree_view_get_item (LrgTreeView *self,
                        guint        position)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), NULL);
	return position < n_visible (self) ? flat_at (self, position)->item : NULL;
}

guint
lrg_tree_view_get_depth (LrgTreeView *self,
                         guint        position)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), 0);
	return position < n_visible (self) ? flat_at (self, position)->depth : 0;
}

guint
lrg_tree_view_get_position (LrgTreeView *self,
                            GObject     *item)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), LRG_TREE_VIEW_INVALID_POSITION);
	return position_of (self, item);
}

void
lrg_tree_view_select_item (LrgTreeView *self,
                           GObject     *item)
{
	g_return_if_fail (LRG_IS_TREE_VIEW (self));
	g_return_if_fail (G_IS_OBJECT (item));

	if (self->mode == LRG_SELECTION_MODE_SINGLE)
		select_only (self, item);
	else if (self->mode == LRG_SELECTION_MODE_MULTIPLE && !g_hash_table_contains (self->selected, item))
	{
		g_hash_table_add (self->selected, g_object_ref (item));
		selection_changed (self);
	}
	g_set_object (&self->cursor, item);
}

void
lrg_tree_view_unselect_all (LrgTreeView *self)
{
	g_return_if_fail (LRG_IS_TREE_VIEW (self));

	if (g_hash_table_size (self->selected) == 0)
		return;
	g_hash_table_remove_all (self->selected);
	selection_changed (self);
}

GObject *
lrg_tree_view_get_selected_item (LrgTreeView *self)
{
	GHashTableIter iter;
	gpointer       key;

	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), NULL);

	if (self->cursor != NULL && g_hash_table_contains (self->selected, self->cursor))
		return self->cursor;
	g_hash_table_iter_init (&iter, self->selected);
	if (g_hash_table_iter_next (&iter, &key, NULL))
		return key;
	return NULL;
}

gboolean
lrg_tree_view_is_item_selected (LrgTreeView *self,
                                GObject     *item)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), FALSE);
	return g_hash_table_contains (self->selected, item);
}

guint
lrg_tree_view_get_n_selected (LrgTreeView *self)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), 0);
	return g_hash_table_size (self->selected);
}

GObject *
lrg_tree_view_get_cursor_item (LrgTreeView *self)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), NULL);
	return self->cursor;
}

void
lrg_tree_view_scroll_to (LrgTreeView *self,
                         guint        position,
                         gboolean     animate)
{
	gfloat pad;
	gfloat vw;
	gfloat vh;
	gfloat rh;
	gfloat top;
	gfloat target;

	g_return_if_fail (LRG_IS_TREE_VIEW (self));

	if (position >= n_visible (self))
		return;

	update_extent (self);
	viewport (self, &pad, &vw, &vh);
	rh = row_h (self);
	top = (gfloat) position * rh;

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
lrg_tree_view_get_scroll_offset (LrgTreeView *self)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), 0.0f);
	return lrg_scroll_axis_get_value (&self->ay);
}

LrgWidget *
lrg_tree_view_get_row_widget (LrgTreeView *self,
                              guint        position)
{
	guint i;

	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), NULL);

	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		if (r->position == position)
			return r->widget;
	}
	return NULL;
}

guint
lrg_tree_view_get_n_rows (LrgTreeView *self)
{
	g_return_val_if_fail (LRG_IS_TREE_VIEW (self), 0);
	return self->rows->len;
}
