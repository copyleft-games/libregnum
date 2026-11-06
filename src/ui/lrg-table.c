/* lrg-table.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTable: A virtualized data table with resizable, sortable columns.
 *
 * Rows come from a GListModel; columns are added by the application
 * with a cell function that returns the text of a cell (drawn by the
 * table itself, no widget needed) or binds a cell widget made by the
 * column's widget factory. Like LrgListView only the rows inside the
 * viewport (plus one buffer row above and below) exist; each is a
 * small private container (LrgTableRow) that carries the hover / press
 * springs of the row and parents its widget cells.
 *
 * The header strip stays put vertically and scrolls sideways with the
 * body. Clicking a sortable header cell flips the sort indicator and
 * emits "sort-changed"; dragging the right border of a resizable
 * header cell resizes the column (the pointer is captured while
 * dragging). Both axes scroll with the shared spring / rubber-band /
 * overlay scrollbar helpers of lrg-scrollbar-private.h.
 *
 * Children, in hit order from the bottom: the row pool, the header hit
 * zone (above rows that peek under the header), then the two
 * scrollbar zones.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include <math.h>
#include <string.h>

#include "lrg-table.h"
#include "lrg-scrollbar-private.h"
#include "../lrg-log.h"

/* Rows kept beyond each end of the visible range */
#define ROW_BUFFER (1)

/* Two presses on one row within this time (canvas clock) activate it */
#define DOUBLE_CLICK_TIME (0.4)

/* Narrowest a column gets, by code or by dragging */
#define MIN_COLUMN_WIDTH (24.0f)

/* Half-width of the grab zone around a resizable column border */
#define RESIZE_GRAB (5.0f)

/* Preferred number of rows when nothing else says otherwise */
#define NATURAL_ROWS (8)

/* Opacity of the SURFACE stripe on odd rows */
#define STRIPE_ALPHA (0.6f)

/* ==========================================================================
 * Flags type
 * ========================================================================== */

GType
lrg_table_column_flags_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_TABLE_COLUMN_NONE, "LRG_TABLE_COLUMN_NONE", "none" },
			{ LRG_TABLE_COLUMN_RESIZABLE, "LRG_TABLE_COLUMN_RESIZABLE", "resizable" },
			{ LRG_TABLE_COLUMN_SORTABLE, "LRG_TABLE_COLUMN_SORTABLE", "sortable" },
			{ LRG_TABLE_COLUMN_EXPAND, "LRG_TABLE_COLUMN_EXPAND", "expand" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgTableColumnFlags"), values);

		g_once_init_leave (&type_id, id);
	}

	return type_id;
}

/* ==========================================================================
 * Private row type: a plain container carrying hover springs and cells
 * ========================================================================== */

#define LRG_TYPE_TABLE_ROW (lrg_table_row_get_type ())
G_DECLARE_FINAL_TYPE (LrgTableRow, lrg_table_row, LRG, TABLE_ROW, LrgContainer)

struct _LrgTableRow
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgTableRow, lrg_table_row, LRG_TYPE_CONTAINER)

/* The table places the cells itself; the row never lays them out */
static void
lrg_table_row_layout_children (LrgContainer *container)
{
}

static void
lrg_table_row_class_init (LrgTableRowClass *klass)
{
	LRG_CONTAINER_CLASS (klass)->layout_children = lrg_table_row_layout_children;
}

static void
lrg_table_row_init (LrgTableRow *self)
{
}

/* ==========================================================================
 * Instance
 * ========================================================================== */

typedef struct
{
	gchar                  *title;
	LrgTableCellFunc        cell_func;
	gpointer                cell_data;
	GDestroyNotify          cell_destroy;
	LrgTableCellCreateFunc  create;
	gpointer                create_data;
	GDestroyNotify          create_destroy;
	GCompareDataFunc        sort_func;
	gpointer                sort_data;
	GDestroyNotify          sort_destroy;
	gfloat                  width;    /* requested width */
	gfloat                  x;        /* offset from the content start (layout) */
	gfloat                  shown;    /* width after the EXPAND share (layout) */
	LrgTableColumnFlags     flags;
} Column;

typedef struct
{
	LrgWidget *widget;    /* LrgTableRow, owned */
	guint      position;  /* bound item, or LRG_TABLE_INVALID_POSITION */
	GPtrArray *texts;     /* gchar * per column (may be NULL) */
	GPtrArray *cells;     /* LrgWidget * per column (NULL for text columns), owned */
	guint      placed;    /* column generation the cells were placed for */
} Row;

struct _LrgTable
{
	LrgContainer      parent_instance;

	GListModel       *model;
	gulong            items_changed_id;

	GPtrArray        *columns;       /* Column *, owned */
	guint             column_gen;    /* bumped when widths change */
	gfloat            content_width; /* sum of shown widths */

	GArray           *rows;          /* Row */
	gfloat            row_height;    /* 0 = theme ROW_HEIGHT */
	gboolean          striped;

	LrgSelectionMode  mode;
	GHashTable       *selected;      /* set of GUINT_TO_POINTER (pos + 1) */
	guint             cursor;
	guint             anchor;

	gint              sort_column;   /* -1 = unsorted */
	gboolean          sort_ascending;
	LrgSpring         sort_flip;     /* 0 ascending .. 1 descending */
	gboolean          stepped;
	gboolean          sorting;       /* a store sort is running */

	LrgWidget        *header;        /* header hit zone, owned */
	gint              hover_column;  /* header cell under the pointer, -1 */
	gint              press_column;  /* header cell pressed, -1 */
	gint              hover_border;  /* resizable border under the pointer, -1 */
	gint              resize_column; /* border being dragged, -1 */
	gfloat            resize_start_x;
	gfloat            resize_start_w;

	LrgScrollAxis     ax;            /* ax.bar owned */
	LrgScrollAxis     ay;            /* ay.bar owned */
	LrgScrollFx       fx;

	gdouble           clock;
	gboolean          click_armed;
	gdouble           last_click_time;
	guint             last_click_pos;
	gboolean          in_layout;
};

G_DEFINE_FINAL_TYPE (LrgTable, lrg_table, LRG_TYPE_CONTAINER)

enum
{
	PROP_0,
	PROP_MODEL,
	PROP_ROW_HEIGHT,
	PROP_STRIPED,
	PROP_SELECTION_MODE,
	PROP_SORT_COLUMN,
	PROP_SORT_ASCENDING,
	N_PROPS
};

enum
{
	SIGNAL_SELECTION_CHANGED,
	SIGNAL_ROW_ACTIVATED,
	SIGNAL_SORT_CHANGED,
	SIGNAL_COLUMN_RESIZED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

#define KEY(pos) (GUINT_TO_POINTER ((pos) + 1))

/* ==========================================================================
 * Small helpers
 * ========================================================================== */

static guint
n_items (LrgTable *self)
{
	return self->model != NULL ? g_list_model_get_n_items (self->model) : 0;
}

/* column_at: column @i; callers make sure it exists */
#define column_at(self, i) ((Column *) g_ptr_array_index ((self)->columns, (i)))

static gfloat
row_h (LrgTable *self)
{
	gfloat h = self->row_height;

	if (h <= 0.0f)
		h = lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_METRIC_ROW_HEIGHT);
	return MAX (h, 1.0f);
}

/* header_h: the header strip is a little taller than a row */
static gfloat
header_h (LrgTable *self)
{
	return roundf (row_h (self) + 4.0f);
}

static gboolean
smooth (LrgTable *self)
{
	return lrg_widget_get_motion (LRG_WIDGET (self)) != LRG_MOTION_NONE;
}

/*
 * body:
 *
 * The scrolling body below the header, relative to the table: x, y,
 * width and height.
 */
static void
body (LrgTable *self,
      gfloat   *bx,
      gfloat   *by,
      gfloat   *bw,
      gfloat   *bh)
{
	LrgWidget *w = LRG_WIDGET (self);
	gfloat     pad = lrg_container_get_padding (LRG_CONTAINER (self));
	gfloat     hh = header_h (self);

	*bx = pad;
	*by = pad + hh;
	*bw = MAX (0.0f, lrg_widget_get_width (w) - pad * 2.0f);
	*bh = MAX (0.0f, lrg_widget_get_height (w) - pad * 2.0f - hh);
}

/* rows_per_page: whole rows that fit, at least one */
static guint
rows_per_page (LrgTable *self)
{
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;

	body (self, &bx, &by, &bw, &bh);
	return MAX (1u, (guint) floorf (bh / row_h (self)));
}

/*
 * compute_columns:
 *
 * Column offsets and shown widths. When every column fits and some
 * carry %LRG_TABLE_COLUMN_EXPAND, those share the space left over.
 * A change bumps the generation so rows re-place their cells.
 */
static void
compute_columns (LrgTable *self)
{
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;
	gfloat total = 0.0f;
	gfloat x = 0.0f;
	gfloat extra;
	guint  n_expand = 0;
	guint  i;
	gboolean changed = FALSE;

	body (self, &bx, &by, &bw, &bh);
	for (i = 0; i < self->columns->len; i++)
	{
		Column *c = column_at (self, i);

		total += c->width;
		if (c->flags & LRG_TABLE_COLUMN_EXPAND)
			n_expand++;
	}
	extra = (n_expand > 0 && total < bw) ? (bw - total) / (gfloat) n_expand : 0.0f;

	for (i = 0; i < self->columns->len; i++)
	{
		Column *c = column_at (self, i);
		gfloat  shown = floorf (c->width + ((c->flags & LRG_TABLE_COLUMN_EXPAND) ? extra : 0.0f));

		if (c->x != x || c->shown != shown)
			changed = TRUE;
		c->x = x;
		c->shown = shown;
		x += shown;
	}
	if (changed || self->content_width != x)
		self->column_gen++;
	self->content_width = x;
}

/* update_extent: both scroll axes from the content size */
static void
update_extent (LrgTable *self)
{
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;

	compute_columns (self);
	body (self, &bx, &by, &bw, &bh);
	lrg_scroll_axis_set_extent (&self->ay, (gfloat) n_items (self) * row_h (self), bh);
	lrg_scroll_axis_set_extent (&self->ax, self->content_width, bw);
}

/* ==========================================================================
 * Property columns
 * ========================================================================== */

/* property_text: an item property as text (transformed from any type) */
static gchar *
property_cell (LrgTable  *self,
               LrgWidget *cell,
               GObject   *item,
               guint      position,
               gpointer   user_data)
{
	const gchar   *name = user_data;
	GParamSpec    *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (item), name);
	GValue         value = G_VALUE_INIT;
	GValue         text = G_VALUE_INIT;
	gchar         *out = NULL;

	if (pspec == NULL || !(pspec->flags & G_PARAM_READABLE))
		return NULL;

	g_value_init (&value, pspec->value_type);
	g_object_get_property (item, name, &value);
	g_value_init (&text, G_TYPE_STRING);
	if (g_value_transform (&value, &text))
		out = g_value_dup_string (&text);
	g_value_unset (&text);
	g_value_unset (&value);
	return out;
}

/*
 * property_compare:
 *
 * Default sort of a property column: numerically when the property
 * transforms to a double, else by g_utf8_collate() of its text.
 */
static gint
property_compare (gconstpointer a,
                  gconstpointer b,
                  gpointer      user_data)
{
	const gchar *name = user_data;
	GObject     *oa = G_OBJECT (a);
	GObject     *ob = G_OBJECT (b);
	GParamSpec  *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (oa), name);
	GValue       va = G_VALUE_INIT;
	GValue       vb = G_VALUE_INIT;
	gint         result = 0;

	if (pspec == NULL || g_object_class_find_property (G_OBJECT_GET_CLASS (ob), name) == NULL)
		return 0;

	g_value_init (&va, pspec->value_type);
	g_value_init (&vb, pspec->value_type);
	g_object_get_property (oa, name, &va);
	g_object_get_property (ob, name, &vb);

	if (pspec->value_type != G_TYPE_STRING && g_value_type_transformable (pspec->value_type, G_TYPE_DOUBLE))
	{
		GValue da = G_VALUE_INIT;
		GValue db = G_VALUE_INIT;

		g_value_init (&da, G_TYPE_DOUBLE);
		g_value_init (&db, G_TYPE_DOUBLE);
		g_value_transform (&va, &da);
		g_value_transform (&vb, &db);
		result = g_value_get_double (&da) < g_value_get_double (&db) ? -1 :
		         g_value_get_double (&da) > g_value_get_double (&db) ? 1 : 0;
	}
	else
	{
		g_autofree gchar *sa = property_cell (NULL, NULL, oa, 0, (gpointer) name);
		g_autofree gchar *sb = property_cell (NULL, NULL, ob, 0, (gpointer) name);

		result = g_utf8_collate (sa != NULL ? sa : "", sb != NULL ? sb : "");
	}

	g_value_unset (&va);
	g_value_unset (&vb);
	return result;
}

/* ==========================================================================
 * Row pool
 * ========================================================================== */

/* unref_nullable: cells of text columns are NULL */
static void
unref_nullable (gpointer data)
{
	if (data != NULL)
		g_object_unref (data);
}

/* row_clear: drop the bound texts (cells stay, they are recycled) */
static void
row_clear_texts (Row *r)
{
	guint i;

	for (i = 0; i < r->texts->len; i++)
		g_clear_pointer (&g_ptr_array_index (r->texts, i), g_free);
}

/* readd_overlays: keep header zone and scrollbars the last children */
static void
remove_overlays (LrgTable *self)
{
	LrgContainer *c = LRG_CONTAINER (self);

	lrg_container_remove_child (c, self->header);
	lrg_container_remove_child (c, self->ay.bar);
	lrg_container_remove_child (c, self->ax.bar);
}

static void
add_overlays (LrgTable *self)
{
	LrgContainer *c = LRG_CONTAINER (self);

	lrg_container_add_child (c, self->header);
	lrg_container_add_child (c, self->ay.bar);
	lrg_container_add_child (c, self->ax.bar);
}

/*
 * grow_pool:
 *
 * Adds @n rows. Each row gets one cell widget per widget column (from
 * the column factory); text columns have no widget. Layout is
 * suppressed while the children change.
 */
static void
grow_pool (LrgTable *self,
           guint     n)
{
	gboolean was = self->in_layout;
	guint    i;
	guint    k;

	if (n == 0)
		return;

	self->in_layout = TRUE;
	remove_overlays (self);
	for (i = 0; i < n; i++)
	{
		Row row;

		row.widget = g_object_new (LRG_TYPE_TABLE_ROW, NULL);
		row.position = LRG_TABLE_INVALID_POSITION;
		row.texts = g_ptr_array_new_full (self->columns->len, g_free);
		row.cells = g_ptr_array_new_full (self->columns->len, unref_nullable);
		row.placed = 0;
		g_ptr_array_set_size (row.texts, (gint) self->columns->len);

		for (k = 0; k < self->columns->len; k++)
		{
			Column    *col = column_at (self, k);
			LrgWidget *cell = NULL;

			if (col->create != NULL)
				cell = col->create (self, k, col->create_data);
			g_ptr_array_add (row.cells, cell);
			if (cell != NULL)
				lrg_container_add_child (LRG_CONTAINER (row.widget), cell);
		}

		lrg_widget_set_visible (row.widget, FALSE);
		lrg_container_add_child (LRG_CONTAINER (self), row.widget);
		g_array_append_val (self->rows, row);
	}
	add_overlays (self);
	self->in_layout = was;
}

/* drop_pool: removes every row (columns changed, dispose) */
static void
drop_pool (LrgTable *self)
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
		g_clear_pointer (&r->texts, g_ptr_array_unref);
		g_clear_pointer (&r->cells, g_ptr_array_unref);
	}
	g_array_set_size (self->rows, 0);
	self->in_layout = was;
}

/* unbind_from: forget bindings at or after @position */
static void
unbind_from (LrgTable *self,
             guint     position)
{
	guint i;

	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		if (r->position != LRG_TABLE_INVALID_POSITION && r->position >= position)
			r->position = LRG_TABLE_INVALID_POSITION;
	}
}

/* bind_row: point @r at @position and run every column's cell func */
static void
bind_row (LrgTable *self,
          Row      *r,
          guint     position)
{
	g_autoptr(GObject) item = g_list_model_get_item (self->model, position);
	guint              k;

	r->position = position;
	row_clear_texts (r);
	if (item == NULL)
		return;

	for (k = 0; k < self->columns->len; k++)
	{
		Column *col = column_at (self, k);

		if (col->cell_func != NULL)
			g_ptr_array_index (r->texts, k) =
				col->cell_func (self, g_ptr_array_index (r->cells, k), item, position, col->cell_data);
	}
}

/* place_cells: put a row's widget cells into their column slots */
static void
place_cells (LrgTable *self,
             Row      *r,
             gfloat    rh)
{
	gfloat pad = lrg_theme_get_metric (lrg_widget_get_theme (LRG_WIDGET (self)), LRG_THEME_METRIC_PADDING_X) * 0.5f;
	guint  k;

	for (k = 0; k < self->columns->len; k++)
	{
		Column    *col = column_at (self, k);
		LrgWidget *cell = g_ptr_array_index (r->cells, k);

		if (cell != NULL)
			lrg_widget_place_in_slot (cell, col->x + pad, 0.0f, MAX (0.0f, col->shown - pad * 2.0f), rh);
	}
	r->placed = self->column_gen;
}

/*
 * sync_rows:
 *
 * Virtualization: works out the positions that must exist for the
 * displayed vertical offset (visible plus ROW_BUFFER each side), keeps
 * rows already bound inside the range, rebinds free rows to missing
 * positions, hides leftovers and places every row (sideways by the
 * horizontal offset).
 */
static void
sync_rows (LrgTable *self)
{
	guint  n = n_items (self);
	gfloat rh = row_h (self);
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;
	gfloat offx;
	gfloat offy;
	gfloat row_w;
	guint  first;
	guint  start;
	guint  end;
	guint  p;
	guint  i;

	body (self, &bx, &by, &bw, &bh);
	offx = lrg_scroll_axis_get_value (&self->ax);
	offy = lrg_scroll_axis_get_value (&self->ay);

	/* 1. Range of positions to realize */
	first = offy > 0.0f ? (guint) floorf (offy / rh) : 0;
	start = first > ROW_BUFFER ? first - ROW_BUFFER : 0;
	end = MIN (n, first + (guint) ceilf (bh / rh) + ROW_BUFFER);
	if (start > end)
		start = end;

	/* 2. Enough rows for it */
	if (self->rows->len < end - start)
		grow_pool (self, (end - start) - self->rows->len);

	/* 3. Free rows bound outside the range */
	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		if (r->position != LRG_TABLE_INVALID_POSITION && (r->position < start || r->position >= end))
			r->position = LRG_TABLE_INVALID_POSITION;
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
			else if (free_row == NULL && r->position == LRG_TABLE_INVALID_POSITION)
				free_row = r;
		}
		if (!shown && free_row != NULL)
			bind_row (self, free_row, p);
	}

	/* 5. Place, show / hide, mark selection */
	row_w = MAX (self->content_width, bw);
	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		if (r->position == LRG_TABLE_INVALID_POSITION)
		{
			lrg_widget_set_visible (r->widget, FALSE);
			continue;
		}

		lrg_widget_set_visible (r->widget, TRUE);
		lrg_widget_allocate (r->widget, bx - offx, by + (gfloat) r->position * rh - offy, row_w, rh);
		if (r->placed != self->column_gen)
			place_cells (self, r, rh);

		if (g_hash_table_contains (self->selected, KEY (r->position)))
			lrg_widget_set_state_flags (r->widget, LRG_WIDGET_STATE_SELECTED);
		else
			lrg_widget_unset_state_flags (r->widget, LRG_WIDGET_STATE_SELECTED);
	}
}

/* rebuild: columns changed; throw the rows away and lay out again */
static void
rebuild (LrgTable *self)
{
	drop_pool (self);
	self->column_gen++;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));
}

/* ==========================================================================
 * Selection
 * ========================================================================== */

static void
selection_changed (LrgTable *self)
{
	g_signal_emit (self, signals[SIGNAL_SELECTION_CHANGED], 0);
}

static void
set_cursor (LrgTable *self,
            guint     position,
            gboolean  reveal)
{
	self->cursor = position;
	if (reveal && position != LRG_TABLE_INVALID_POSITION)
		lrg_table_scroll_to (self, position, TRUE);
}

static void
select_only (LrgTable *self,
             guint     position)
{
	if (self->mode == LRG_SELECTION_MODE_NONE)
		return;
	if (g_hash_table_size (self->selected) == 1 && g_hash_table_contains (self->selected, KEY (position)))
		return;
	g_hash_table_remove_all (self->selected);
	g_hash_table_add (self->selected, KEY (position));
	selection_changed (self);
}

static void
select_range (LrgTable *self,
              guint     a,
              guint     b)
{
	guint lo = MIN (a, b);
	guint hi = MAX (a, b);
	guint p;

	g_hash_table_remove_all (self->selected);
	for (p = lo; p <= hi; p++)
		g_hash_table_add (self->selected, KEY (p));
	selection_changed (self);
}

/* move_cursor_to: keyboard movement; Shift extends in multiple mode */
static void
move_cursor_to (LrgTable *self,
                guint     position,
                gboolean  extend)
{
	guint n = n_items (self);

	if (n == 0)
		return;
	position = MIN (position, n - 1);

	if (extend && self->mode == LRG_SELECTION_MODE_MULTIPLE && self->anchor != LRG_TABLE_INVALID_POSITION)
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
 * Shifts the selection and cursor past the edit, drops removed
 * selected rows and rebinds rows at or after @position. A sort run by
 * the table itself restores the selection afterwards, so it is left
 * alone here.
 */
static void
on_items_changed (GListModel *model,
                  guint       position,
                  guint       removed,
                  guint       added,
                  LrgTable   *self)
{
	GHashTable     *moved;
	GHashTableIter  iter;
	gpointer        key;
	gboolean        changed = FALSE;
	guint           n = n_items (self);

	if (!self->sorting)
	{
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

		if (self->cursor != LRG_TABLE_INVALID_POSITION && self->cursor >= position)
		{
			if (self->cursor >= position + removed)
				self->cursor = self->cursor - removed + added;
			else
				self->cursor = n > 0 ? MIN (position, n - 1) : LRG_TABLE_INVALID_POSITION;
		}
		if (self->cursor != LRG_TABLE_INVALID_POSITION && self->cursor >= n)
			self->cursor = n > 0 ? n - 1 : LRG_TABLE_INVALID_POSITION;
		self->anchor = self->cursor;
	}

	unbind_from (self, position);
	update_extent (self);
	sync_rows (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));

	if (changed)
		selection_changed (self);
}

/* sort_compare: the column's ascending comparison, reversed if needed */
static gint
sort_compare (gconstpointer a,
              gconstpointer b,
              gpointer      user_data)
{
	LrgTable *self = LRG_TABLE (user_data);
	Column   *col = column_at (self, (guint) self->sort_column);
	gint      r = col->sort_func (a, b, col->sort_data);

	return self->sort_ascending ? r : -r;
}

/*
 * apply_sort:
 *
 * Sorts a GListStore model by the sort column's function, keeping the
 * selected items (and the cursor item) selected at their new places.
 */
static void
apply_sort (LrgTable *self)
{
	Column         *col;
	GPtrArray      *keep;
	GObject        *cursor_item = NULL;
	GHashTableIter  iter;
	gpointer        key;
	guint           n;
	guint           p;

	if (self->sort_column < 0 || self->model == NULL || !G_IS_LIST_STORE (self->model))
		return;
	if ((guint) self->sort_column >= self->columns->len)
		return;
	col = column_at (self, (guint) self->sort_column);
	if (col->sort_func == NULL)
		return;

	/* 1. Remember the selected items */
	keep = g_ptr_array_new_with_free_func (g_object_unref);
	g_hash_table_iter_init (&iter, self->selected);
	while (g_hash_table_iter_next (&iter, &key, NULL))
	{
		GObject *item = g_list_model_get_item (self->model, GPOINTER_TO_UINT (key) - 1);

		if (item != NULL)
			g_ptr_array_add (keep, item);
	}
	if (self->cursor != LRG_TABLE_INVALID_POSITION)
		cursor_item = g_list_model_get_item (self->model, self->cursor);

	/* 2. Sort (items-changed rebinds every row) */
	self->sorting = TRUE;
	g_list_store_sort (G_LIST_STORE (self->model), sort_compare, self);
	self->sorting = FALSE;

	/* 3. Find them again */
	g_hash_table_remove_all (self->selected);
	n = n_items (self);
	for (p = 0; p < n && (keep->len > 0 || cursor_item != NULL); p++)
	{
		g_autoptr(GObject) item = g_list_model_get_item (self->model, p);
		guint              idx;

		if (g_ptr_array_find (keep, item, &idx))
		{
			g_hash_table_add (self->selected, KEY (p));
			g_ptr_array_remove_index_fast (keep, idx);
		}
		if (item == cursor_item)
		{
			self->cursor = p;
			self->anchor = p;
			g_clear_object (&cursor_item);
		}
	}
	g_clear_object (&cursor_item);
	g_ptr_array_unref (keep);
	sync_rows (self);
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

static void
lrg_table_layout_children (LrgContainer *container)
{
	LrgTable *self = LRG_TABLE (container);
	gfloat    bx;
	gfloat    by;
	gfloat    bw;
	gfloat    bh;
	gfloat    zone;
	gboolean  vshow;
	gboolean  hshow;
	gfloat    pad;

	if (self->in_layout || self->ay.bar == NULL)
		return;
	self->in_layout = TRUE;

	update_extent (self);
	body (self, &bx, &by, &bw, &bh);
	pad = lrg_container_get_padding (container);

	/* Header hit zone over the header strip */
	lrg_widget_allocate (self->header, pad, pad, bw, header_h (self));

	/* Scrollbar zones along the right and bottom edges of the body */
	zone = lrg_scroll_bar_zone (LRG_WIDGET (self));
	vshow = lrg_scroll_axis_get_max (&self->ay) > 0.0f;
	hshow = lrg_scroll_axis_get_max (&self->ax) > 0.0f;
	lrg_widget_set_visible (self->ay.bar, vshow);
	lrg_widget_set_visible (self->ax.bar, hshow);
	lrg_widget_allocate (self->ay.bar, bx + bw - zone, by, zone, MAX (0.0f, bh - (hshow ? zone : 0.0f)));
	lrg_widget_allocate (self->ax.bar, bx, by + bh - zone, MAX (0.0f, bw - (vshow ? zone : 0.0f)), zone);

	sync_rows (self);
	self->in_layout = FALSE;
}

static void
lrg_table_measure (LrgWidget *widget,
                   gfloat    *preferred_width,
                   gfloat    *preferred_height)
{
	LrgTable *self = LRG_TABLE (widget);
	gfloat    pad = lrg_container_get_padding (LRG_CONTAINER (self));
	gfloat    total = 0.0f;
	guint     rows = MIN (n_items (self), NATURAL_ROWS);
	guint     i;

	/* All columns at their requested widths, up to NATURAL_ROWS rows
	 * (measuring every item would defeat virtualization) */
	for (i = 0; i < self->columns->len; i++)
		total += column_at (self, i)->width;

	*preferred_width = MAX (total, 120.0f) + pad * 2.0f;
	*preferred_height = header_h (self) + (gfloat) MAX (rows, 1u) * row_h (self) + pad * 2.0f;
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

static void
lrg_table_style_defaults (LrgWidget      *widget,
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
		/* A recessed well like a list */
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme,
		                          (state & LRG_WIDGET_STATE_FOCUSED) ? LRG_THEME_COLOR_ACCENT
		                                                             : LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		lrg_part_look_set_radius (look, radius);
		if (state & LRG_WIDGET_STATE_DISABLED)
			lrg_part_look_set_opacity (look, 0.45f);
		return;

	case LRG_UI_PART_HEADER:
		/* Header strip / cell: control colour, hover and press shades
		 * for sortable cells; muted titles, the sorted one in TEXT */
		if (state & LRG_WIDGET_STATE_PRESSED)
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_PRESSED));
		else if (state & LRG_WIDGET_STATE_HOVERED)
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL_HOVER));
		else
			lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL));
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme,
		                              (state & (LRG_WIDGET_STATE_SELECTED | LRG_WIDGET_STATE_HOVERED))
		                              ? LRG_THEME_COLOR_TEXT : LRG_THEME_COLOR_TEXT_MUTED));
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_BORDER),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_BORDER_WIDTH));
		return;

	case LRG_UI_PART_ITEM:
		/* Row backgrounds: odd rows (CHECKED) carry a SURFACE stripe;
		 * hover, press and selection as in a list */
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
		else if (state & LRG_WIDGET_STATE_CHECKED)
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), STRIPE_ALPHA, &c);
		else
			lrg_paint_color_with_alpha (lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), 0.0f, &c);
		lrg_part_look_set_background (look, &c);
		return;

	case LRG_UI_PART_LABEL:
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_TEXT));
		return;

	case LRG_UI_PART_ARROW:
		/* Sort indicator */
		lrg_part_look_set_foreground (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_size (look, lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE) * 0.5f);
		return;

	case LRG_UI_PART_INDICATOR:
		/* Keyboard cursor outline, and a dragged / hovered column border */
		lrg_part_look_set_border (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_FOCUS),
		                          lrg_theme_get_metric (theme, LRG_THEME_METRIC_FOCUS_RING_WIDTH) * 0.75f);
		lrg_part_look_set_background (look, lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT));
		lrg_part_look_set_radius (look, 2.0f);
		return;

	default:
		break;
	}

	LRG_WIDGET_CLASS (lrg_table_parent_class)->style_defaults (widget, part, state, look);
}

/* ==========================================================================
 * Painting
 * ========================================================================== */

/* paint_text_v: text with its left edge at @x, centred on @cy */
static void
paint_text_v (LrgPaintList   *list,
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

/* paint_row_background: the ITEM look of one row, blended by its springs */
static void
paint_row_background (LrgTable     *self,
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
	if (self->striped && (r->position & 1u))
		base |= LRG_WIDGET_STATE_CHECKED;

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
	if (look.background.a > 0 || (look.set & LRG_PART_LOOK_GRADIENT))
		lrg_widget_paint_box (widget, list, &look, x, y, w, h);

	/* Cursor outline after keyboard / gamepad navigation */
	if (r->position == self->cursor &&
	    (lrg_widget_get_state_flags (widget) & LRG_WIDGET_STATE_FOCUS_VISIBLE))
	{
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_INDICATOR, base, &variant);
		lrg_paint_list_rect_ex (list, x + 1.0f, y + 1.0f, MAX (0.0f, w - 2.0f), MAX (0.0f, h - 2.0f),
		                        &variant.radii, NULL, NULL, variant.border_width, &variant.border);
	}
}

/* paint_chevron: a sort chevron at (@cx, @cy) turned by @flip half-turns */
static void
paint_chevron (LrgPaintList   *list,
               gfloat          cx,
               gfloat          cy,
               gfloat          s,
               gfloat          flip,
               const GrlColor *color)
{
	gfloat angle = flip * (gfloat) G_PI;
	gfloat ca = cosf (angle);
	gfloat sa = sinf (angle);
	/* Pointing up (ascending) at rest */
	gfloat px[3] = { -s * 0.5f, 0.0f, s * 0.5f };
	gfloat py[3] = { s * 0.25f, -s * 0.25f, s * 0.25f };
	gfloat rx[3];
	gfloat ry[3];
	gfloat t = MAX (1.5f, s * 0.2f);
	guint  k;

	for (k = 0; k < 3; k++)
	{
		rx[k] = cx + px[k] * ca - py[k] * sa;
		ry[k] = cy + px[k] * sa + py[k] * ca;
	}
	lrg_paint_list_line (list, rx[0], ry[0], rx[1], ry[1], t, color);
	lrg_paint_list_line (list, rx[1], ry[1], rx[2], ry[2], t, color);
}

/*
 * paint_header:
 *
 * The header strip (HEADER part, top corners rounded like the box),
 * per-cell hover / press highlight for sortable cells, titles, the
 * sort chevron (ARROW) and column separators. A hovered or dragged
 * resizable border is drawn in the INDICATOR background (accent).
 */
static void
paint_header (LrgTable     *self,
              LrgPaintList *list,
              gfloat        wx,
              gfloat        wy)
{
	LrgWidget   *widget = LRG_WIDGET (self);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	LrgPartLook  strip;
	LrgPartLook  arrow;
	LrgPartLook  ind;
	gfloat       pad = lrg_container_get_padding (LRG_CONTAINER (self));
	gfloat       hh = header_h (self);
	gfloat       bx;
	gfloat       by;
	gfloat       bw;
	gfloat       bh;
	gfloat       x0;
	gfloat       y0 = wy + pad;
	gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       tpad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X) * 0.5f;
	gfloat       radius = lrg_theme_get_metric (theme, LRG_THEME_METRIC_CORNER_RADIUS);
	gfloat       flip = self->stepped ? self->sort_flip.value : (self->sort_ascending ? 0.0f : 1.0f);
	LrgCornerRadii radii;
	guint        i;

	body (self, &bx, &by, &bw, &bh);
	x0 = wx + bx - lrg_scroll_axis_get_value (&self->ax);

	/* 1. Strip, inside the box border, rounded on top */
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_HEADER, LRG_WIDGET_STATE_NONE, &strip);
	radii.top_left = radii.top_right = MAX (0.0f, radius - pad);
	radii.bottom_left = radii.bottom_right = 0.0f;
	lrg_paint_list_rect_ex (list, wx + pad, y0, bw, hh, &radii, &strip.background,
	                        (strip.set & LRG_PART_LOOK_GRADIENT) ? &strip.gradient : NULL, 0.0f, NULL);
	lrg_paint_list_rect (list, wx + pad, y0 + hh - MAX (1.0f, strip.border_width), bw,
	                     MAX (1.0f, strip.border_width), 0.0f, &strip.border);

	lrg_widget_resolve_look (widget, LRG_UI_PART_ARROW, &arrow);
	lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_INDICATOR, LRG_WIDGET_STATE_NONE, &ind);

	lrg_paint_list_push_clip (list, wx + pad, y0, bw, hh);
	for (i = 0; i < self->columns->len; i++)
	{
		Column        *col = column_at (self, i);
		LrgWidgetState st = LRG_WIDGET_STATE_NONE;
		LrgPartLook    cell;
		gfloat         cx = x0 + col->x;
		gfloat         tw = col->shown - tpad * 2.0f;
		gboolean       sorted = self->sort_column == (gint) i;

		/* 2. Cell highlight */
		if (sorted)
			st |= LRG_WIDGET_STATE_SELECTED;
		if ((col->flags & LRG_TABLE_COLUMN_SORTABLE) && self->hover_column == (gint) i &&
		    self->resize_column < 0 && self->hover_border < 0)
			st |= LRG_WIDGET_STATE_HOVERED;
		if (self->press_column == (gint) i)
			st |= LRG_WIDGET_STATE_PRESSED | LRG_WIDGET_STATE_HOVERED;
		lrg_widget_resolve_look_for_state (widget, LRG_UI_PART_HEADER, st, &cell);
		if (st & (LRG_WIDGET_STATE_HOVERED | LRG_WIDGET_STATE_PRESSED))
			lrg_paint_list_rect (list, cx, y0, col->shown, hh - MAX (1.0f, strip.border_width), 0.0f,
			                     &cell.background);

		/* 3. Title, leaving room for the chevron of the sorted column */
		if (sorted)
			tw -= size * 0.5f + tpad;
		lrg_paint_list_push_clip (list, cx + tpad, y0, MAX (0.0f, tw), hh);
		paint_text_v (list, theme, col->title, cx + tpad, y0 + hh * 0.5f, size, &cell.foreground);
		lrg_paint_list_pop_clip (list);

		/* 4. Sort chevron, flipping with a spring */
		if (sorted)
		{
			gfloat s = arrow.size > 0.0f ? arrow.size : size * 0.5f;

			paint_chevron (list, cx + col->shown - tpad - s * 0.5f, y0 + hh * 0.5f, s, flip,
			               &arrow.foreground);
		}

		/* 5. Separator at the right border; accent while resizable
		 *    border is hovered or dragged */
		if (self->resize_column == (gint) i || self->hover_border == (gint) i)
			lrg_paint_list_rect (list, cx + col->shown - 1.5f, y0 + 3.0f, 3.0f, hh - 6.0f, 1.5f,
			                     &ind.background);
		else if (i + 1 < self->columns->len || (col->flags & LRG_TABLE_COLUMN_RESIZABLE))
			lrg_paint_list_rect (list, cx + col->shown - 1.0f, y0 + hh * 0.25f, 1.0f, hh * 0.5f, 0.0f,
			                     &strip.border);
	}
	lrg_paint_list_pop_clip (list);
}

/*
 * lrg_table_step:
 *
 * Advances the canvas clock, both scroll springs, the scrollbar fade
 * and the sort chevron flip; rebinds rows for the new offsets.
 */
static gboolean
lrg_table_step (LrgWidget *widget,
                gfloat     delta_time)
{
	LrgTable *self = LRG_TABLE (widget);
	gboolean  moving;
	gboolean  keep;

	if (delta_time > 0.0f)
		self->clock += (gdouble) delta_time;
	if (self->ay.bar == NULL)
		return FALSE;

	/* Sort chevron: snapped on the first frame */
	lrg_spring_set_motion (&self->sort_flip, lrg_widget_get_motion (widget));
	lrg_spring_set_target (&self->sort_flip, self->sort_ascending ? 0.0f : 1.0f);
	if (!self->stepped || lrg_widget_get_motion (widget) == LRG_MOTION_NONE)
	{
		lrg_spring_snap (&self->sort_flip, self->sort_flip.target);
		self->stepped = TRUE;
		moving = FALSE;
	}
	else
		moving = lrg_spring_step (&self->sort_flip, delta_time);

	if (lrg_scroll_axis_step_snap (&self->ay, delta_time) | lrg_scroll_axis_step_snap (&self->ax, delta_time))
	{
		moving = TRUE;
		lrg_scroll_fx_poke (&self->fx);
	}
	sync_rows (self);

	keep = self->ay.dragging || self->ax.dragging ||
	       (lrg_widget_get_state_flags (self->ay.bar) & LRG_WIDGET_STATE_HOVERED) ||
	       (lrg_widget_get_state_flags (self->ax.bar) & LRG_WIDGET_STATE_HOVERED);
	return lrg_scroll_fx_update (&self->fx, keep, delta_time) || moving;
}

static void
lrg_table_paint (LrgWidget    *widget,
                 LrgPaintList *list)
{
	LrgTable    *self = LRG_TABLE (widget);
	LrgTheme    *theme = lrg_widget_get_theme (widget);
	LrgPartLook  box;
	LrgPartLook  label;
	gfloat       wx = lrg_widget_get_world_x (widget);
	gfloat       wy = lrg_widget_get_world_y (widget);
	gfloat       rh = row_h (self);
	gfloat       size = lrg_theme_get_metric (theme, LRG_THEME_METRIC_FONT_SIZE);
	gfloat       tpad = lrg_theme_get_metric (theme, LRG_THEME_METRIC_PADDING_X) * 0.5f;
	gfloat       bx;
	gfloat       by;
	gfloat       bw;
	gfloat       bh;
	gfloat       offx;
	gfloat       offy;
	gfloat       row_w;
	guint        i;
	guint        k;

	/* 1. Rows follow the displayed offsets */
	update_extent (self);
	sync_rows (self);

	/* 2. Body */
	lrg_widget_resolve_look (widget, LRG_UI_PART_BOX, &box);
	lrg_widget_paint_box (widget, list, &box, wx, wy, lrg_widget_get_width (widget), lrg_widget_get_height (widget));

	/* 3. Rows, clipped to the body: background, text cells, widgets */
	body (self, &bx, &by, &bw, &bh);
	offx = lrg_scroll_axis_get_value (&self->ax);
	offy = lrg_scroll_axis_get_value (&self->ay);
	row_w = MAX (self->content_width, bw);
	lrg_widget_resolve_look (widget, LRG_UI_PART_LABEL, &label);
	lrg_paint_list_push_clip (list, wx + bx, wy + by, bw, bh);
	for (i = 0; i < self->rows->len; i++)
	{
		const Row *r = &g_array_index (self->rows, Row, i);
		gfloat     ry;
		gfloat     rx;

		if (r->position == LRG_TABLE_INVALID_POSITION)
			continue;
		rx = wx + bx - offx;
		ry = wy + by + (gfloat) r->position * rh - offy;
		paint_row_background (self, list, r, wx + bx, ry, MAX (bw, row_w - offx), rh);

		for (k = 0; k < self->columns->len; k++)
		{
			Column      *col = column_at (self, k);
			const gchar *text = g_ptr_array_index (r->texts, k);

			if (text == NULL || col->shown <= tpad * 2.0f)
				continue;
			lrg_paint_list_push_clip (list, rx + col->x + tpad, ry, col->shown - tpad * 2.0f, rh);
			paint_text_v (list, theme, text, rx + col->x + tpad, ry + rh * 0.5f, size, &label.foreground);
			lrg_paint_list_pop_clip (list);
		}
		lrg_widget_paint (r->widget, list);
	}
	lrg_paint_list_pop_clip (list);

	/* 4. Header on top of rows peeking under it */
	paint_header (self, list, wx, wy);

	/* 5. Overlay scrollbars */
	lrg_scroll_bar_paint (widget, list, &self->ay, TRUE, self->fx.fade.value);
	lrg_scroll_bar_paint (widget, list, &self->ax, FALSE, self->fx.fade.value);
}

/* ==========================================================================
 * Events
 * ========================================================================== */

/* position_at: the row under a world y inside the body, or invalid */
static guint
position_at (LrgTable *self,
             gfloat    y)
{
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;
	gfloat rel;
	guint  p;

	body (self, &bx, &by, &bw, &bh);
	rel = y - (lrg_widget_get_world_y (LRG_WIDGET (self)) + by);
	if (rel < 0.0f || rel >= bh)
		return LRG_TABLE_INVALID_POSITION;
	rel += lrg_scroll_axis_get_value (&self->ay);
	if (rel < 0.0f)
		return LRG_TABLE_INVALID_POSITION;
	p = (guint) floorf (rel / row_h (self));
	return p < n_items (self) ? p : LRG_TABLE_INVALID_POSITION;
}

/* in_header: is a world y on the header strip? */
static gboolean
in_header (LrgTable *self,
           gfloat    y)
{
	gfloat pad = lrg_container_get_padding (LRG_CONTAINER (self));
	gfloat top = lrg_widget_get_world_y (LRG_WIDGET (self)) + pad;

	return y >= top && y < top + header_h (self);
}

/* content_x: a world x as an offset from the start of the columns */
static gfloat
content_x (LrgTable *self,
           gfloat    x)
{
	gfloat pad = lrg_container_get_padding (LRG_CONTAINER (self));

	return x - (lrg_widget_get_world_x (LRG_WIDGET (self)) + pad) + lrg_scroll_axis_get_value (&self->ax);
}

/* column_at_x: the column under a world x, or -1 */
static gint
column_at_x (LrgTable *self,
             gfloat    x)
{
	gfloat cx = content_x (self, x);
	guint  i;

	for (i = 0; i < self->columns->len; i++)
	{
		Column *col = column_at (self, i);

		if (cx >= col->x && cx < col->x + col->shown)
			return (gint) i;
	}
	return -1;
}

/* border_at_x: the resizable column whose right border is under x, or -1 */
static gint
border_at_x (LrgTable *self,
             gfloat    x)
{
	gfloat cx = content_x (self, x);
	gint   i;

	/* From the right so a narrow column's border wins over its left neighbour */
	for (i = (gint) self->columns->len - 1; i >= 0; i--)
	{
		Column *col = column_at (self, (guint) i);

		if ((col->flags & LRG_TABLE_COLUMN_RESIZABLE) && fabsf (cx - (col->x + col->shown)) <= RESIZE_GRAB)
			return i;
	}
	return -1;
}

/* track_header_hover: update the hovered cell / border from the pointer */
static void
track_header_hover (LrgTable *self,
                    gfloat    x,
                    gfloat    y)
{
	if (in_header (self, y) && lrg_widget_contains_point (LRG_WIDGET (self), x, y))
	{
		self->hover_border = border_at_x (self, x);
		self->hover_column = column_at_x (self, x);
	}
	else
	{
		self->hover_border = -1;
		self->hover_column = -1;
	}
}

/* click_row: select (Ctrl toggles, Shift ranges), double click activates */
static void
click_row (LrgTable          *self,
           guint              p,
           LrgInputModifiers  mods)
{
	if (self->mode == LRG_SELECTION_MODE_MULTIPLE && (mods & LRG_INPUT_MODIFIER_SHIFT) &&
	    self->anchor != LRG_TABLE_INVALID_POSITION)
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
handle_key (LrgTable          *self,
            GrlKey             key,
            LrgInputModifiers  mods)
{
	guint    n = n_items (self);
	guint    cur = self->cursor;
	guint    page = rows_per_page (self);
	gboolean extend = (mods & LRG_INPUT_MODIFIER_SHIFT) != 0;
	gboolean none = cur == LRG_TABLE_INVALID_POSITION;

	switch (key)
	{
	case GRL_KEY_UP:
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
	case GRL_KEY_LEFT:
	case GRL_KEY_RIGHT:
		/* Sideways scrolling only when the columns are wider than the
		 * view; otherwise focus navigation gets the key */
		if (lrg_scroll_axis_get_max (&self->ax) <= 0.0f)
			return FALSE;
		lrg_scroll_axis_scroll_to (&self->ax, self->ax.pos.target +
		                           (key == GRL_KEY_LEFT ? -LRG_SCROLL_WHEEL_STEP : LRG_SCROLL_WHEEL_STEP),
		                           smooth (self));
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;
	default:
		return FALSE;
	}
}

/* header_press: start a resize or arm a sort click */
static void
header_press (LrgTable *self,
              gfloat    x)
{
	gint border = border_at_x (self, x);

	if (border >= 0)
	{
		Column *col = column_at (self, (guint) border);

		self->resize_column = border;
		self->resize_start_x = x;
		self->resize_start_w = col->shown;
		return;
	}

	self->press_column = column_at_x (self, x);
	if (self->press_column >= 0 &&
	    !(column_at (self, (guint) self->press_column)->flags & LRG_TABLE_COLUMN_SORTABLE))
		self->press_column = -1;
}

/* header_release: a release on the pressed sortable cell toggles the sort */
static void
header_release (LrgTable *self,
                gfloat    x,
                gfloat    y)
{
	gint col = self->press_column;

	self->press_column = -1;
	if (col < 0 || !in_header (self, y) || column_at_x (self, x) != col)
		return;

	/* New column: ascending; same column: flip */
	if (self->sort_column == col)
		lrg_table_set_sort (self, col, !self->sort_ascending);
	else
		lrg_table_set_sort (self, col, TRUE);
}

static gboolean
lrg_table_handle_event (LrgWidget        *widget,
                        const LrgUIEvent *event)
{
	LrgTable  *self = LRG_TABLE (widget);
	LrgWidget *target = lrg_ui_event_get_target (event);
	gfloat     x = lrg_ui_event_get_x (event);
	gfloat     y = lrg_ui_event_get_y (event);

	switch (lrg_ui_event_get_event_type (event))
	{
	case LRG_UI_EVENT_POINTER_ENTER:
		lrg_scroll_fx_poke (&self->fx);
		return FALSE;

	case LRG_UI_EVENT_POINTER_LEAVE:
		if (target == LRG_WIDGET (self) || target == self->header)
		{
			self->hover_column = -1;
			self->hover_border = -1;
		}
		return FALSE;

	case LRG_UI_EVENT_MOUSE_MOVE:
		lrg_scroll_fx_poke (&self->fx);
		if (self->resize_column >= 0)
		{
			/* Captured drag: the border follows the pointer */
			Column *col = column_at (self, (guint) self->resize_column);
			gfloat  w = MAX (MIN_COLUMN_WIDTH, self->resize_start_w + (x - self->resize_start_x));
			gfloat  share = col->shown - col->width;

			lrg_table_set_column_width (self, (guint) self->resize_column, MAX (MIN_COLUMN_WIDTH, w - share));
			return TRUE;
		}
		if (self->ay.dragging)
		{
			lrg_scroll_bar_drag (&self->ay, TRUE, y);
			return TRUE;
		}
		if (self->ax.dragging)
		{
			lrg_scroll_bar_drag (&self->ax, FALSE, x);
			return TRUE;
		}
		track_header_hover (self, x, y);
		return FALSE;

	case LRG_UI_EVENT_MOUSE_BUTTON_DOWN:
		{
			guint p;

			if (lrg_ui_event_get_button (event) != 0)
				return FALSE;
			update_extent (self);
			if (target == self->ay.bar)
			{
				lrg_scroll_bar_press (&self->ay, TRUE, y, smooth (self));
				lrg_scroll_fx_poke (&self->fx);
				return TRUE;
			}
			if (target == self->ax.bar)
			{
				lrg_scroll_bar_press (&self->ax, FALSE, x, smooth (self));
				lrg_scroll_fx_poke (&self->fx);
				return TRUE;
			}
			if (in_header (self, y))
			{
				header_press (self, x);
				return TRUE;
			}
			p = position_at (self, y);
			if (p == LRG_TABLE_INVALID_POSITION)
				return FALSE;
			click_row (self, p, lrg_ui_event_get_modifiers (event));
			return TRUE;
		}

	case LRG_UI_EVENT_MOUSE_BUTTON_UP:
		if (lrg_ui_event_get_button (event) != 0)
			return FALSE;
		if (self->resize_column >= 0)
		{
			Column *col = column_at (self, (guint) self->resize_column);
			guint   c = (guint) self->resize_column;

			self->resize_column = -1;
			track_header_hover (self, x, y);
			g_signal_emit (self, signals[SIGNAL_COLUMN_RESIZED], 0, c, col->width);
			return TRUE;
		}
		if (self->ay.dragging || self->ax.dragging)
		{
			self->ay.dragging = FALSE;
			self->ax.dragging = FALSE;
			return TRUE;
		}
		if (self->press_column >= 0)
		{
			header_release (self, x, y);
			return TRUE;
		}
		return FALSE;

	case LRG_UI_EVENT_PAN:
		{
			/* Touch drag: claim it along the axis it mostly follows */
			gfloat   dx = lrg_ui_event_get_delta_x (event);
			gfloat   dy = lrg_ui_event_get_delta_y (event);
			gboolean handled = FALSE;

			update_extent (self);
			if (fabsf (dy) >= fabsf (dx) * 0.5f)
				handled |= lrg_scroll_axis_pan (&self->ay, dy);
			if (fabsf (dx) >= fabsf (dy) * 0.5f)
				handled |= lrg_scroll_axis_pan (&self->ax, dx);
			if (handled)
				lrg_scroll_fx_poke (&self->fx);
			return handled;
		}

	case LRG_UI_EVENT_PAN_END:
		lrg_scroll_axis_flick (&self->ay, smooth (self) ? lrg_ui_event_get_velocity_y (event) : 0.0f);
		lrg_scroll_axis_flick (&self->ax, smooth (self) ? lrg_ui_event_get_velocity_x (event) : 0.0f);
		lrg_scroll_fx_poke (&self->fx);
		return TRUE;

	case LRG_UI_EVENT_SCROLL:
		{
			gfloat   dy = -lrg_ui_event_get_scroll_y (event) * LRG_SCROLL_WHEEL_STEP;
			gfloat   dx = -lrg_ui_event_get_scroll_x (event) * LRG_SCROLL_WHEEL_STEP;
			gboolean handled = FALSE;

			update_extent (self);
			/* Shift turns the vertical wheel sideways */
			if (lrg_ui_event_get_modifiers (event) & LRG_INPUT_MODIFIER_SHIFT)
			{
				dx += dy;
				dy = 0.0f;
			}
			if (dy != 0.0f)
				handled |= lrg_scroll_axis_wheel (&self->ay, dy, smooth (self));
			if (dx != 0.0f)
				handled |= lrg_scroll_axis_wheel (&self->ax, dx, smooth (self));
			if (handled)
				lrg_scroll_fx_poke (&self->fx);
			return handled;
		}

	case LRG_UI_EVENT_KEY_DOWN:
		update_extent (self);
		return handle_key (self, lrg_ui_event_get_key (event), lrg_ui_event_get_modifiers (event));

	case LRG_UI_EVENT_ACTIVATE:
		if (self->cursor == LRG_TABLE_INVALID_POSITION || self->cursor >= n_items (self))
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
column_free (gpointer data)
{
	Column *c = data;

	if (c->cell_destroy != NULL && c->cell_data != NULL)
		c->cell_destroy (c->cell_data);
	if (c->create_destroy != NULL && c->create_data != NULL)
		c->create_destroy (c->create_data);
	if (c->sort_destroy != NULL && c->sort_data != NULL)
		c->sort_destroy (c->sort_data);
	g_free (c->title);
	g_free (c);
}

static void
lrg_table_constructed (GObject *object)
{
	LrgTable *self = LRG_TABLE (object);

	G_OBJECT_CLASS (lrg_table_parent_class)->constructed (object);

	self->in_layout = TRUE;
	self->header = g_object_new (LRG_TYPE_WIDGET, NULL);
	lrg_widget_set_name (self->header, "header");
	self->ay.bar = lrg_scroll_bar_new ("vscrollbar");
	self->ax.bar = lrg_scroll_bar_new ("hscrollbar");
	add_overlays (self);
	self->in_layout = FALSE;
}

static void
lrg_table_dispose (GObject *object)
{
	LrgTable *self = LRG_TABLE (object);

	if (self->model != NULL)
	{
		g_clear_signal_handler (&self->items_changed_id, self->model);
		g_clear_object (&self->model);
	}
	if (self->rows != NULL)
		drop_pool (self);
	g_clear_object (&self->header);
	g_clear_object (&self->ay.bar);
	g_clear_object (&self->ax.bar);

	G_OBJECT_CLASS (lrg_table_parent_class)->dispose (object);
}

static void
lrg_table_finalize (GObject *object)
{
	LrgTable *self = LRG_TABLE (object);

	g_clear_pointer (&self->rows, g_array_unref);
	g_clear_pointer (&self->selected, g_hash_table_unref);
	g_clear_pointer (&self->columns, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_table_parent_class)->finalize (object);
}

static void
lrg_table_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	LrgTable *self = LRG_TABLE (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		g_value_set_object (value, self->model);
		break;
	case PROP_ROW_HEIGHT:
		g_value_set_float (value, self->row_height);
		break;
	case PROP_STRIPED:
		g_value_set_boolean (value, self->striped);
		break;
	case PROP_SELECTION_MODE:
		g_value_set_enum (value, self->mode);
		break;
	case PROP_SORT_COLUMN:
		g_value_set_int (value, self->sort_column);
		break;
	case PROP_SORT_ASCENDING:
		g_value_set_boolean (value, self->sort_ascending);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_table_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	LrgTable *self = LRG_TABLE (object);

	switch (prop_id)
	{
	case PROP_MODEL:
		lrg_table_set_model (self, g_value_get_object (value));
		break;
	case PROP_ROW_HEIGHT:
		lrg_table_set_row_height (self, g_value_get_float (value));
		break;
	case PROP_STRIPED:
		lrg_table_set_striped (self, g_value_get_boolean (value));
		break;
	case PROP_SELECTION_MODE:
		lrg_table_set_selection_mode (self, (LrgSelectionMode) g_value_get_enum (value));
		break;
	case PROP_SORT_COLUMN:
		lrg_table_set_sort (self, g_value_get_int (value), self->sort_ascending);
		break;
	case PROP_SORT_ASCENDING:
		lrg_table_set_sort (self, self->sort_column, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_table_class_init (LrgTableClass *klass)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (klass);
	LrgWidgetClass    *widget_class = LRG_WIDGET_CLASS (klass);
	LrgContainerClass *container_class = LRG_CONTAINER_CLASS (klass);

	object_class->constructed = lrg_table_constructed;
	object_class->dispose = lrg_table_dispose;
	object_class->finalize = lrg_table_finalize;
	object_class->get_property = lrg_table_get_property;
	object_class->set_property = lrg_table_set_property;

	widget_class->measure = lrg_table_measure;
	widget_class->paint = lrg_table_paint;
	widget_class->step = lrg_table_step;
	widget_class->style_defaults = lrg_table_style_defaults;
	widget_class->handle_event = lrg_table_handle_event;

	container_class->layout_children = lrg_table_layout_children;

	/**
	 * LrgTable:model:
	 *
	 * The #GListModel whose items are the rows.
	 */
	properties[PROP_MODEL] =
		g_param_spec_object ("model", "Model", "The rows to show",
		                     G_TYPE_LIST_MODEL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTable:row-height:
	 *
	 * Height of every row; 0 uses the theme's ROW_HEIGHT metric. The
	 * header is 4 units taller.
	 */
	properties[PROP_ROW_HEIGHT] =
		g_param_spec_float ("row-height", "Row height", "Height of every row (0 = theme)",
		                    0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTable:striped:
	 *
	 * Whether odd rows are tinted (the ITEM part in the CHECKED state).
	 */
	properties[PROP_STRIPED] =
		g_param_spec_boolean ("striped", "Striped", "Tint odd rows",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTable:selection-mode:
	 *
	 * How many rows can be selected.
	 */
	properties[PROP_SELECTION_MODE] =
		g_param_spec_enum ("selection-mode", "Selection mode", "How many rows can be selected",
		                   LRG_TYPE_SELECTION_MODE, LRG_SELECTION_MODE_SINGLE,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTable:sort-column:
	 *
	 * The column showing the sort indicator, or -1.
	 */
	properties[PROP_SORT_COLUMN] =
		g_param_spec_int ("sort-column", "Sort column", "Sorted column (-1 = none)",
		                  -1, G_MAXINT, -1,
		                  G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgTable:sort-ascending:
	 *
	 * Direction of the sort indicator.
	 */
	properties[PROP_SORT_ASCENDING] =
		g_param_spec_boolean ("sort-ascending", "Sort ascending", "Sort direction",
		                      TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgTable::selection-changed:
	 * @self: the table
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
	 * LrgTable::row-activated:
	 * @self: the table
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

	/**
	 * LrgTable::sort-changed:
	 * @self: the table
	 * @column: the sorted column, or -1
	 * @ascending: the direction
	 *
	 * Emitted when the sort indicator moved or flipped (header click
	 * or lrg_table_set_sort()). Sorting the model is the application's
	 * job unless the column has a sort function and the model is a
	 * #GListStore (lrg_table_set_sort_func()); then the store is
	 * already sorted when this is emitted.
	 */
	signals[SIGNAL_SORT_CHANGED] =
		g_signal_new ("sort-changed",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_BOOLEAN);

	/**
	 * LrgTable::column-resized:
	 * @self: the table
	 * @column: the resized column
	 * @width: its new width
	 *
	 * Emitted when a drag of a column border ends.
	 */
	signals[SIGNAL_COLUMN_RESIZED] =
		g_signal_new ("column-resized",
		              G_TYPE_FROM_CLASS (klass),
		              G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_FLOAT);
}

static void
lrg_table_init (LrgTable *self)
{
	self->columns = g_ptr_array_new_with_free_func (column_free);
	self->rows = g_array_new (FALSE, TRUE, sizeof (Row));
	self->selected = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->mode = LRG_SELECTION_MODE_SINGLE;
	self->striped = TRUE;
	self->cursor = LRG_TABLE_INVALID_POSITION;
	self->anchor = LRG_TABLE_INVALID_POSITION;
	self->last_click_pos = LRG_TABLE_INVALID_POSITION;
	self->sort_column = -1;
	self->sort_ascending = TRUE;
	self->hover_column = -1;
	self->press_column = -1;
	self->hover_border = -1;
	self->resize_column = -1;
	self->column_gen = 1;
	lrg_spring_init (&self->sort_flip, 0.0f, LRG_MOTION_SMOOTH);
	lrg_scroll_axis_init (&self->ax);
	lrg_scroll_axis_init (&self->ay);
	lrg_scroll_fx_init (&self->fx);

	lrg_container_set_padding (LRG_CONTAINER (self), 1.0f);
	lrg_widget_set_focusable (LRG_WIDGET (self), TRUE);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgTable *
lrg_table_new (void)
{
	return g_object_new (LRG_TYPE_TABLE, NULL);
}

void
lrg_table_set_model (LrgTable   *self,
                     GListModel *model)
{
	gboolean had_selection;

	g_return_if_fail (LRG_IS_TABLE (self));
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

	had_selection = g_hash_table_size (self->selected) > 0;
	g_hash_table_remove_all (self->selected);
	self->cursor = LRG_TABLE_INVALID_POSITION;
	self->anchor = LRG_TABLE_INVALID_POSITION;
	unbind_from (self, 0);
	lrg_scroll_axis_scroll_to (&self->ay, 0.0f, FALSE);
	lrg_widget_queue_layout (LRG_WIDGET (self));
	lrg_container_layout_children (LRG_CONTAINER (self));

	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
	if (had_selection)
		selection_changed (self);
}

GListModel *
lrg_table_get_model (LrgTable *self)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), NULL);
	return self->model;
}

guint
lrg_table_add_column (LrgTable            *self,
                      const gchar         *title,
                      LrgTableCellFunc     cell_func,
                      gpointer             user_data,
                      GDestroyNotify       destroy,
                      gfloat               width,
                      LrgTableColumnFlags  flags)
{
	Column *col;

	g_return_val_if_fail (LRG_IS_TABLE (self), 0);

	col = g_new0 (Column, 1);
	col->title = g_strdup (title != NULL ? title : "");
	col->cell_func = cell_func;
	col->cell_data = user_data;
	col->cell_destroy = destroy;
	col->width = MAX (MIN_COLUMN_WIDTH, width);
	col->flags = flags;
	g_ptr_array_add (self->columns, col);

	rebuild (self);
	return self->columns->len - 1;
}

guint
lrg_table_add_property_column (LrgTable            *self,
                               const gchar         *title,
                               const gchar         *property,
                               gfloat               width,
                               LrgTableColumnFlags  flags)
{
	guint index;

	g_return_val_if_fail (LRG_IS_TABLE (self), 0);
	g_return_val_if_fail (property != NULL, 0);

	index = lrg_table_add_column (self, title, property_cell, g_strdup (property), g_free, width, flags);
	if (flags & LRG_TABLE_COLUMN_SORTABLE)
		lrg_table_set_sort_func (self, index, property_compare, g_strdup (property), g_free);
	return index;
}

void
lrg_table_set_column_widget_factory (LrgTable               *self,
                                     guint                   column,
                                     LrgTableCellCreateFunc  create,
                                     gpointer                user_data,
                                     GDestroyNotify          destroy)
{
	Column *col;

	g_return_if_fail (LRG_IS_TABLE (self));
	g_return_if_fail (column < self->columns->len);
	col = column_at (self, column);

	/* Old cells came from the old factory: rebuild the rows first */
	drop_pool (self);
	if (col->create_destroy != NULL && col->create_data != NULL)
		col->create_destroy (col->create_data);
	col->create = create;
	col->create_data = user_data;
	col->create_destroy = destroy;
	rebuild (self);
}

guint
lrg_table_get_n_columns (LrgTable *self)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), 0);
	return self->columns->len;
}

const gchar *
lrg_table_get_column_title (LrgTable *self,
                            guint     column)
{
	Column *col;

	g_return_val_if_fail (LRG_IS_TABLE (self), NULL);
	if (column >= self->columns->len)
		return NULL;
	col = column_at (self, column);
	return col->title;
}

void
lrg_table_set_column_title (LrgTable    *self,
                            guint        column,
                            const gchar *title)
{
	Column *col;

	g_return_if_fail (LRG_IS_TABLE (self));
	g_return_if_fail (column < self->columns->len);
	col = column_at (self, column);

	g_free (col->title);
	col->title = g_strdup (title != NULL ? title : "");
}

gfloat
lrg_table_get_column_width (LrgTable *self,
                            guint     column)
{
	Column *col;

	g_return_val_if_fail (LRG_IS_TABLE (self), 0.0f);
	if (column >= self->columns->len)
		return 0.0f;
	col = column_at (self, column);
	return col->width;
}

void
lrg_table_set_column_width (LrgTable *self,
                            guint     column,
                            gfloat    width)
{
	Column *col;

	g_return_if_fail (LRG_IS_TABLE (self));
	g_return_if_fail (column < self->columns->len);
	col = column_at (self, column);

	width = MAX (MIN_COLUMN_WIDTH, roundf (width));
	if (col->width == width)
		return;
	col->width = width;
	update_extent (self);
	lrg_widget_queue_layout (LRG_WIDGET (self));
}

LrgTableColumnFlags
lrg_table_get_column_flags (LrgTable *self,
                            guint     column)
{
	Column *col;

	g_return_val_if_fail (LRG_IS_TABLE (self), LRG_TABLE_COLUMN_NONE);
	if (column >= self->columns->len)
		return LRG_TABLE_COLUMN_NONE;
	col = column_at (self, column);
	return col->flags;
}

void
lrg_table_set_sort_func (LrgTable         *self,
                         guint             column,
                         GCompareDataFunc  func,
                         gpointer          user_data,
                         GDestroyNotify    destroy)
{
	Column *col;

	g_return_if_fail (LRG_IS_TABLE (self));
	g_return_if_fail (column < self->columns->len);
	col = column_at (self, column);

	if (col->sort_destroy != NULL && col->sort_data != NULL)
		col->sort_destroy (col->sort_data);
	col->sort_func = func;
	col->sort_data = user_data;
	col->sort_destroy = destroy;
}

void
lrg_table_set_sort (LrgTable *self,
                    gint      column,
                    gboolean  ascending)
{
	gboolean col_changed;
	gboolean dir_changed;

	g_return_if_fail (LRG_IS_TABLE (self));

	if (column >= (gint) self->columns->len)
		column = -1;
	column = MAX (column, -1);
	ascending = !!ascending;
	col_changed = self->sort_column != column;
	dir_changed = self->sort_ascending != ascending;
	if (!col_changed && !dir_changed)
		return;

	/* A new column shows its chevron without flipping in */
	self->sort_column = column;
	self->sort_ascending = ascending;
	if (col_changed)
		lrg_spring_snap (&self->sort_flip, ascending ? 0.0f : 1.0f);

	apply_sort (self);

	if (col_changed)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SORT_COLUMN]);
	if (dir_changed)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SORT_ASCENDING]);
	g_signal_emit (self, signals[SIGNAL_SORT_CHANGED], 0, column, ascending);
}

gint
lrg_table_get_sort_column (LrgTable *self)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), -1);
	return self->sort_column;
}

gboolean
lrg_table_get_sort_ascending (LrgTable *self)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), TRUE);
	return self->sort_ascending;
}

gfloat
lrg_table_get_row_height (LrgTable *self)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), 0.0f);
	return self->row_height;
}

void
lrg_table_set_row_height (LrgTable *self,
                          gfloat    row_height)
{
	g_return_if_fail (LRG_IS_TABLE (self));

	row_height = MAX (row_height, 0.0f);
	if (self->row_height == row_height)
		return;
	self->row_height = row_height;
	self->column_gen++;
	lrg_widget_queue_layout (LRG_WIDGET (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ROW_HEIGHT]);
}

gboolean
lrg_table_get_striped (LrgTable *self)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), FALSE);
	return self->striped;
}

void
lrg_table_set_striped (LrgTable *self,
                       gboolean  striped)
{
	g_return_if_fail (LRG_IS_TABLE (self));

	striped = !!striped;
	if (self->striped == striped)
		return;
	self->striped = striped;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STRIPED]);
}

LrgSelectionMode
lrg_table_get_selection_mode (LrgTable *self)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), LRG_SELECTION_MODE_NONE);
	return self->mode;
}

void
lrg_table_set_selection_mode (LrgTable         *self,
                              LrgSelectionMode  mode)
{
	g_return_if_fail (LRG_IS_TABLE (self));

	if (self->mode == mode)
		return;
	self->mode = mode;

	if (mode == LRG_SELECTION_MODE_NONE && g_hash_table_size (self->selected) > 0)
	{
		g_hash_table_remove_all (self->selected);
		selection_changed (self);
	}
	else if (mode == LRG_SELECTION_MODE_SINGLE && g_hash_table_size (self->selected) > 1)
		select_only (self, lrg_table_get_selected (self));
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SELECTION_MODE]);
}

guint
lrg_table_get_selected (LrgTable *self)
{
	GHashTableIter iter;
	gpointer       key;
	guint          best = LRG_TABLE_INVALID_POSITION;

	g_return_val_if_fail (LRG_IS_TABLE (self), LRG_TABLE_INVALID_POSITION);

	if (self->cursor != LRG_TABLE_INVALID_POSITION && g_hash_table_contains (self->selected, KEY (self->cursor)))
		return self->cursor;
	g_hash_table_iter_init (&iter, self->selected);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		best = MIN (best, GPOINTER_TO_UINT (key) - 1);
	return best;
}

guint
lrg_table_get_n_selected (LrgTable *self)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), 0);
	return g_hash_table_size (self->selected);
}

gboolean
lrg_table_is_selected (LrgTable *self,
                       guint     position)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), FALSE);
	return g_hash_table_contains (self->selected, KEY (position));
}

void
lrg_table_select (LrgTable *self,
                  guint     position)
{
	g_return_if_fail (LRG_IS_TABLE (self));

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
lrg_table_unselect_all (LrgTable *self)
{
	g_return_if_fail (LRG_IS_TABLE (self));

	if (g_hash_table_size (self->selected) == 0)
		return;
	g_hash_table_remove_all (self->selected);
	selection_changed (self);
}

guint
lrg_table_get_cursor (LrgTable *self)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), LRG_TABLE_INVALID_POSITION);
	return self->cursor;
}

void
lrg_table_scroll_to (LrgTable *self,
                     guint     position,
                     gboolean  animate)
{
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;
	gfloat rh;
	gfloat top;
	gfloat target;

	g_return_if_fail (LRG_IS_TABLE (self));

	if (position >= n_items (self))
		return;

	update_extent (self);
	body (self, &bx, &by, &bw, &bh);
	rh = row_h (self);
	top = (gfloat) position * rh;

	target = self->ay.bounce ? lrg_scroll_axis_clamp (&self->ay, self->ay.pos.target) : self->ay.pos.target;
	if (top + rh > target + bh)
		target = top + rh - bh;
	if (top < target)
		target = top;

	lrg_scroll_axis_scroll_to (&self->ay, target, animate && smooth (self));
	lrg_scroll_fx_poke (&self->fx);
	if (!(animate && smooth (self)))
		sync_rows (self);
}

void
lrg_table_get_scroll_offset (LrgTable *self,
                             gfloat   *x,
                             gfloat   *y)
{
	g_return_if_fail (LRG_IS_TABLE (self));

	if (x != NULL)
		*x = lrg_scroll_axis_get_value (&self->ax);
	if (y != NULL)
		*y = lrg_scroll_axis_get_value (&self->ay);
}

guint
lrg_table_get_n_rows (LrgTable *self)
{
	g_return_val_if_fail (LRG_IS_TABLE (self), 0);
	return self->rows->len;
}

/* find_row: the realized row bound to @position, or NULL */
static Row *
find_row (LrgTable *self,
          guint     position)
{
	guint i;

	for (i = 0; i < self->rows->len; i++)
	{
		Row *r = &g_array_index (self->rows, Row, i);

		if (r->position == position)
			return r;
	}
	return NULL;
}

const gchar *
lrg_table_get_cell_text (LrgTable *self,
                         guint     position,
                         guint     column)
{
	Row *r;

	g_return_val_if_fail (LRG_IS_TABLE (self), NULL);

	r = position != LRG_TABLE_INVALID_POSITION ? find_row (self, position) : NULL;
	if (r == NULL || column >= r->texts->len)
		return NULL;
	return g_ptr_array_index (r->texts, column);
}

LrgWidget *
lrg_table_get_cell_widget (LrgTable *self,
                           guint     position,
                           guint     column)
{
	Row *r;

	g_return_val_if_fail (LRG_IS_TABLE (self), NULL);

	r = position != LRG_TABLE_INVALID_POSITION ? find_row (self, position) : NULL;
	if (r == NULL || column >= r->cells->len)
		return NULL;
	return g_ptr_array_index (r->cells, column);
}
