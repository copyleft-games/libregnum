/* test-ui-data-widgets.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Table, tree view, tile view, splitter, expander, wrap box.
 *
 * Headless: widgets sit on a canvas, are driven through
 * lrg_canvas_process_input() like the real input loop and stepped with
 * lrg_canvas_update() at 60 Hz, so animations are deterministic. One
 * GPU case renders the table under a hidden window and probes pixels;
 * it skips without a display.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

/* ==========================================================================
 * Test item: a name and a value
 * ========================================================================== */

#define TEST_TYPE_ITEM (test_item_get_type ())
G_DECLARE_FINAL_TYPE (TestItem, test_item, TEST, ITEM, GObject)

struct _TestItem
{
	GObject  parent_instance;
	gchar   *name;
	gint     value;
};

G_DEFINE_FINAL_TYPE (TestItem, test_item, G_TYPE_OBJECT)

enum
{
	ITEM_PROP_0,
	ITEM_PROP_NAME,
	ITEM_PROP_VALUE
};

static void
test_item_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
	TestItem *self = TEST_ITEM (object);

	if (prop_id == ITEM_PROP_NAME)
		g_value_set_string (value, self->name);
	else if (prop_id == ITEM_PROP_VALUE)
		g_value_set_int (value, self->value);
}

static void
test_item_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
	TestItem *self = TEST_ITEM (object);

	if (prop_id == ITEM_PROP_NAME)
	{
		g_free (self->name);
		self->name = g_value_dup_string (value);
	}
	else if (prop_id == ITEM_PROP_VALUE)
		self->value = g_value_get_int (value);
}

static void
test_item_finalize (GObject *object)
{
	g_free (TEST_ITEM (object)->name);
	G_OBJECT_CLASS (test_item_parent_class)->finalize (object);
}

static void
test_item_class_init (TestItemClass *klass)
{
	GObjectClass *oc = G_OBJECT_CLASS (klass);

	oc->get_property = test_item_get_property;
	oc->set_property = test_item_set_property;
	oc->finalize = test_item_finalize;
	g_object_class_install_property (oc, ITEM_PROP_NAME,
	                                 g_param_spec_string ("name", NULL, NULL, NULL, G_PARAM_READWRITE));
	g_object_class_install_property (oc, ITEM_PROP_VALUE,
	                                 g_param_spec_int ("value", NULL, NULL, G_MININT, G_MAXINT, 0,
	                                                   G_PARAM_READWRITE));
}

static void
test_item_init (TestItem *self)
{
}

static TestItem *
item_new (const gchar *name,
          gint         value)
{
	return g_object_new (TEST_TYPE_ITEM, "name", name, "value", value, NULL);
}

/* store_new: @n items "item-i" whose value runs backwards (n - i) */
static GListStore *
store_new (guint n)
{
	GListStore *store = g_list_store_new (TEST_TYPE_ITEM);
	GPtrArray  *items = g_ptr_array_new_with_free_func (g_object_unref);
	guint       i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("item-%u", i);

		g_ptr_array_add (items, item_new (name, (gint) (n - i)));
	}
	g_list_store_splice (store, 0, 0, items->pdata, items->len);
	g_ptr_array_unref (items);
	return store;
}

static gint
item_value_at (GListModel *model,
               guint       i)
{
	g_autoptr(TestItem) item = g_list_model_get_item (model, i);

	return item->value;
}

/* ==========================================================================
 * Canvas and input helpers
 * ========================================================================== */

static LrgCanvas *
canvas_new (void)
{
	LrgCanvas *canvas = lrg_canvas_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), 800.0f, 600.0f);
	lrg_canvas_set_paint_background (canvas, FALSE);
	return canvas;
}

/* place: add @widget to the canvas at a fixed rectangle (canvas owns it) */
static gpointer
place (LrgCanvas *canvas,
       gpointer   widget,
       gfloat     x,
       gfloat     y,
       gfloat     w,
       gfloat     h)
{
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (widget));
	g_object_unref (widget);
	lrg_widget_allocate (LRG_WIDGET (widget), x, y, w, h);
	lrg_canvas_record (canvas);
	return widget;
}

static void
frame_pointer (LrgCanvasInput *input,
               gfloat          x,
               gfloat          y)
{
	lrg_canvas_input_init (input);
	input->pointer_x = x;
	input->pointer_y = y;
	input->delta_time = DT;
}

static void
move_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	lrg_canvas_process_input (canvas, &input);
}

static void
press_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
drag_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
release_at (LrgCanvas *canvas,
            gfloat     x,
            gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
click_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	move_to (canvas, x, y);
	press_at (canvas, x, y);
	release_at (canvas, x, y);
}

/* key: one frame with a key press */
static void
key (LrgCanvas *canvas,
     GrlKey     k)
{
	LrgCanvasInput input;

	frame_pointer (&input, -10.0f, -10.0f);
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

static void
activate (LrgCanvas *canvas)
{
	LrgCanvasInput input;

	frame_pointer (&input, -10.0f, -10.0f);
	input.activate = TRUE;
	lrg_canvas_process_input (canvas, &input);
}

/* touch_frame: one frame with one finger at (@x, @y), or none when @down is FALSE */
static void
touch_frame (LrgCanvas *canvas,
             gboolean   down,
             gfloat     x,
             gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, -100.0f, -100.0f);
	if (down)
		lrg_canvas_input_add_touch (&input, 0, x, y);
	lrg_canvas_process_input (canvas, &input);
}

/* settle: run frames until nothing animates (at most @max) */
static guint
settle (LrgCanvas *canvas,
        guint      max)
{
	guint n = 0;

	while (n < max && lrg_canvas_update (canvas, DT))
	{
		lrg_canvas_record (canvas);
		n++;
	}
	lrg_canvas_record (canvas);
	g_assert_cmpuint (n, <, max);
	return n;
}

static void
on_count_swapped (gpointer user_data)
{
	(*(guint *) user_data)++;
}

/* ==========================================================================
 * Table
 *
 * Fixture geometry: table at (0, 0), 300 x 200, padding 1, rows 24 high,
 * header 28 high (y 1..29), columns "Name" 100 (x 1..101, resizable)
 * and "Value" 80 (x 101..181, sortable).
 * ========================================================================== */

#define T_ROW    (24.0f)
#define T_BODY_Y (29.0f)

typedef struct
{
	guint calls;
} CellCounter;

static gchar *
counting_name_cell (LrgTable  *table,
                    LrgWidget *cell,
                    GObject   *item,
                    guint      position,
                    gpointer   user_data)
{
	if (user_data != NULL)
		((CellCounter *) user_data)->calls++;
	return g_strdup (TEST_ITEM (item)->name);
}

static LrgTable *
table_fixture (LrgCanvas   *canvas,
               guint        n,
               GListStore **out_store,
               CellCounter *counter)
{
	LrgTable   *table = lrg_table_new ();
	GListStore *store = store_new (n);

	lrg_table_set_row_height (table, T_ROW);
	lrg_table_add_column (table, "Name", counting_name_cell, counter, NULL, 100.0f,
	                      LRG_TABLE_COLUMN_RESIZABLE);
	lrg_table_add_property_column (table, "Value", "value", 80.0f, LRG_TABLE_COLUMN_SORTABLE);
	lrg_table_set_model (table, G_LIST_MODEL (store));
	place (canvas, table, 0.0f, 0.0f, 300.0f, 200.0f);

	if (out_store != NULL)
		*out_store = store;
	else
		g_object_unref (store);
	return table;
}

static void
test_table_virtualized (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	CellCounter          counter = { 0 };
	LrgTable            *table = table_fixture (canvas, 10000, NULL, &counter);
	guint                rows;
	guint                calls;

	/* Body 170 high -> 8 rows visible (7.08), plus a buffer row */
	rows = lrg_table_get_n_rows (table);
	g_assert_cmpuint (rows, >=, 8);
	g_assert_cmpuint (rows, <=, 10);
	g_assert_cmpstr (lrg_table_get_cell_text (table, 0, 0), ==, "item-0");
	g_assert_cmpstr (lrg_table_get_cell_text (table, 3, 1), ==, "9997");
	g_assert_null (lrg_table_get_cell_text (table, 100, 0));

	/* Cells are bound once, not per frame */
	calls = counter.calls;
	settle (canvas, 120);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (counter.calls, ==, calls);

	/* Jumping far recycles the same rows */
	lrg_table_scroll_to (table, 5000, FALSE);
	lrg_canvas_record (canvas);
	g_assert_cmpstr (lrg_table_get_cell_text (table, 5000, 0), ==, "item-5000");
	g_assert_null (lrg_table_get_cell_text (table, 0, 0));
	g_assert_cmpuint (lrg_table_get_n_rows (table), <=, 10);
	/* Rows + header zone + two scrollbars */
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (table)), ==,
	                  lrg_table_get_n_rows (table) + 3);
}

typedef struct
{
	guint    count;
	gint     column;
	gboolean ascending;
} SortLog;

static void
on_sort_changed (LrgTable *table,
                 gint      column,
                 gboolean  ascending,
                 gpointer  user_data)
{
	SortLog *log = user_data;

	log->count++;
	log->column = column;
	log->ascending = ascending;
}

/* by_name_length_desc: a custom order (longest names first, then name) */
static gint
by_name_length (gconstpointer a,
                gconstpointer b,
                gpointer      user_data)
{
	const TestItem *ia = a;
	const TestItem *ib = b;
	gint            d = (gint) strlen (ia->name) - (gint) strlen (ib->name);

	return d != 0 ? -d : g_strcmp0 (ia->name, ib->name);
}

static void
test_table_sort (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	g_autoptr(GListStore) store = NULL;
	SortLog               log = { 0, -2, FALSE };
	LrgTable             *table = table_fixture (canvas, 50, &store, NULL);
	gfloat                hy = 15.0f;

	g_signal_connect (table, "sort-changed", G_CALLBACK (on_sort_changed), &log);
	g_assert_cmpint (lrg_table_get_sort_column (table), ==, -1);

	/* Select item-0 (value 50) so we can see the selection follow it */
	click_at (canvas, 50.0f, T_BODY_Y + 5.0f);
	g_assert_true (lrg_table_is_selected (table, 0));

	/* Click the sortable "Value" header: ascending, store sorted */
	click_at (canvas, 140.0f, hy);
	g_assert_cmpuint (log.count, ==, 1);
	g_assert_cmpint (log.column, ==, 1);
	g_assert_true (log.ascending);
	g_assert_cmpint (item_value_at (G_LIST_MODEL (store), 0), ==, 1);
	g_assert_cmpint (item_value_at (G_LIST_MODEL (store), 49), ==, 50);
	g_assert_true (lrg_table_is_selected (table, 49));
	g_assert_cmpuint (lrg_table_get_n_selected (table), ==, 1);
	lrg_canvas_record (canvas);
	g_assert_cmpstr (lrg_table_get_cell_text (table, 0, 1), ==, "1");

	/* Again: descending; the chevron flips with a spring */
	click_at (canvas, 140.0f, hy);
	g_assert_cmpuint (log.count, ==, 2);
	g_assert_false (log.ascending);
	g_assert_false (lrg_table_get_sort_ascending (table));
	g_assert_cmpint (item_value_at (G_LIST_MODEL (store), 0), ==, 50);
	g_assert_true (lrg_canvas_update (canvas, DT) || lrg_motion_get_reduced ());
	settle (canvas, 240);

	/* "Name" is not sortable: no signal */
	click_at (canvas, 40.0f, hy);
	g_assert_cmpuint (log.count, ==, 2);

	/* A custom sort function through the API */
	lrg_table_set_sort_func (table, 0, by_name_length, NULL, NULL);
	lrg_table_set_sort (table, 0, TRUE);
	g_assert_cmpuint (log.count, ==, 3);
	g_assert_cmpint (lrg_table_get_sort_column (table), ==, 0);
	{
		g_autoptr(TestItem) first = g_list_model_get_item (G_LIST_MODEL (store), 0);

		g_assert_cmpstr (first->name, ==, "item-10");
	}
}

static void
on_resized (LrgTable *table,
            guint     column,
            gfloat    width,
            gpointer  user_data)
{
	*(gfloat *) user_data = width;
}

static void
test_table_resize (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTable            *table = table_fixture (canvas, 20, NULL, NULL);
	SortLog              log = { 0, -2, FALSE };
	gfloat               resized = 0.0f;

	g_signal_connect (table, "column-resized", G_CALLBACK (on_resized), &resized);
	g_signal_connect (table, "sort-changed", G_CALLBACK (on_sort_changed), &log);

	/* Grab the right border of "Name" (x = 101) and drag 50 right */
	move_to (canvas, 101.0f, 15.0f);
	press_at (canvas, 101.0f, 15.0f);
	drag_to (canvas, 130.0f, 15.0f);
	drag_to (canvas, 151.0f, 40.0f);   /* the capture keeps the drag */
	g_assert_cmpfloat (lrg_table_get_column_width (table, 0), ==, 150.0f);
	release_at (canvas, 151.0f, 40.0f);
	g_assert_cmpfloat (resized, ==, 150.0f);
	g_assert_cmpuint (log.count, ==, 0);

	/* The value column moved with it */
	lrg_canvas_record (canvas);
	click_at (canvas, 190.0f, 15.0f);
	g_assert_cmpuint (log.count, ==, 1);

	/* Dragging far left clamps to the minimum */
	move_to (canvas, 151.0f, 15.0f);
	press_at (canvas, 151.0f, 15.0f);
	drag_to (canvas, -200.0f, 15.0f);
	release_at (canvas, -200.0f, 15.0f);
	g_assert_cmpfloat (lrg_table_get_column_width (table, 0), ==, 24.0f);

	/* Wide columns scroll sideways with Left / Right */
	lrg_table_set_column_width (table, 0, 600.0f);
	lrg_canvas_record (canvas);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (table));
	key (canvas, GRL_KEY_RIGHT);
	settle (canvas, 240);
	{
		gfloat x;

		lrg_table_get_scroll_offset (table, &x, NULL);
		g_assert_cmpfloat (x, ==, 48.0f);
	}
}

static void
on_activated (gpointer instance,
              guint    position,
              gpointer user_data)
{
	*(guint *) user_data = position;
}

static void
test_table_keyboard (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTable            *table = table_fixture (canvas, 1000, NULL, NULL);
	guint                activated = G_MAXUINT;
	gfloat               y;

	g_signal_connect (table, "row-activated", G_CALLBACK (on_activated), &activated);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (table));

	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpuint (lrg_table_get_cursor (table), ==, 0);
	key (canvas, GRL_KEY_DOWN);
	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpuint (lrg_table_get_selected (table), ==, 2);
	key (canvas, GRL_KEY_PAGE_DOWN);
	g_assert_cmpuint (lrg_table_get_cursor (table), ==, 9);
	key (canvas, GRL_KEY_END);
	g_assert_cmpuint (lrg_table_get_cursor (table), ==, 999);
	settle (canvas, 300);
	lrg_table_get_scroll_offset (table, NULL, &y);
	g_assert_cmpfloat (y, ==, 1000.0f * T_ROW - 170.0f);
	g_assert_nonnull (lrg_table_get_cell_text (table, 999, 0));
	key (canvas, GRL_KEY_HOME);
	g_assert_cmpuint (lrg_table_get_cursor (table), ==, 0);

	/* Left / Right are not claimed when the columns fit */
	key (canvas, GRL_KEY_LEFT);
	activate (canvas);
	g_assert_cmpuint (activated, ==, 0);

	/* Double click activates */
	settle (canvas, 300);
	click_at (canvas, 50.0f, T_BODY_Y + T_ROW + 5.0f);
	click_at (canvas, 50.0f, T_BODY_Y + T_ROW + 5.0f);
	g_assert_cmpuint (activated, ==, 1);
}

static void
test_table_pan (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTable            *table = table_fixture (canvas, 200, NULL, NULL);
	gfloat               y = 180.0f;
	gfloat               off;
	gfloat               after;
	gint                 i;

	/* Finger down in the body, drag up 100 units */
	touch_frame (canvas, TRUE, 100.0f, y);
	for (i = 0; i < 10; i++)
	{
		y -= 10.0f;
		touch_frame (canvas, TRUE, 100.0f, y);
	}
	lrg_table_get_scroll_offset (table, NULL, &off);
	g_assert_cmpfloat (off, >, 80.0f);
	g_assert_nonnull (lrg_table_get_cell_text (table, 5, 0));

	/* Lift with speed: the content coasts further */
	touch_frame (canvas, FALSE, 0.0f, 0.0f);
	settle (canvas, 300);
	lrg_table_get_scroll_offset (table, NULL, &after);
	g_assert_cmpfloat (after, >, off + 30.0f);
}

/* ==========================================================================
 * Tree view
 *
 * Roots "root-i"; every root has 5 children "root-i/j"; "root-i/0" has
 * 3 grandchildren; deeper items are leaves. Tree at (0, 0), 200 x 300,
 * padding 4, rows 24, indent 18: the arrow column of depth d spans
 * x = 6 + 18 d .. 24 + 18 d.
 * ========================================================================== */

typedef struct
{
	guint queries;
} TreeLog;

static GListModel *
tree_children (LrgTreeView *tree,
               GObject     *item,
               gpointer     user_data)
{
	const gchar *name = TEST_ITEM (item)->name;
	GListStore  *store;
	guint        n;
	guint        i;
	gint         slashes = 0;
	const gchar *c;

	((TreeLog *) user_data)->queries++;
	for (c = name; *c != '\0'; c++)
		slashes += *c == '/';
	if (slashes >= 2 || (slashes == 1 && !g_str_has_suffix (name, "/0")))
		return NULL;

	n = slashes == 0 ? 5 : 3;
	store = g_list_store_new (TEST_TYPE_ITEM);
	for (i = 0; i < n; i++)
	{
		g_autofree gchar   *child = g_strdup_printf ("%s/%u", name, i);
		g_autoptr(TestItem) it = item_new (child, (gint) i);

		g_list_store_append (store, it);
	}
	return G_LIST_MODEL (store);
}

static LrgTreeView *
tree_fixture (LrgCanvas *canvas,
              guint      n,
              TreeLog   *log)
{
	LrgTreeView          *tree = lrg_tree_view_new ();
	g_autoptr(GListStore) store = g_list_store_new (TEST_TYPE_ITEM);
	GPtrArray            *items = g_ptr_array_new_with_free_func (g_object_unref);
	guint                 i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("root-%u", i);

		g_ptr_array_add (items, item_new (name, (gint) i));
	}
	g_list_store_splice (store, 0, 0, items->pdata, items->len);
	g_ptr_array_unref (items);

	lrg_tree_view_set_row_height (tree, 24.0f);
	lrg_tree_view_set_children_func (tree, tree_children, log, NULL);
	lrg_tree_view_set_model (tree, G_LIST_MODEL (store));
	place (canvas, tree, 0.0f, 0.0f, 200.0f, 300.0f);
	return tree;
}

static const gchar *
tree_name (LrgTreeView *tree,
           guint        p)
{
	GObject *item = lrg_tree_view_get_item (tree, p);

	return item != NULL ? TEST_ITEM (item)->name : NULL;
}

/* tree_row_y: canvas y of the middle of row @p (unscrolled) */
static gfloat
tree_row_y (guint p)
{
	return 4.0f + 24.0f * (gfloat) p + 12.0f;
}

static void
test_tree_virtualized (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	TreeLog              log = { 0 };
	LrgTreeView         *tree = tree_fixture (canvas, 10000, &log);
	guint                rows;

	/* 292 / 24 -> 13 rows visible, plus a buffer row */
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree), ==, 10000);
	rows = lrg_tree_view_get_n_rows (tree);
	g_assert_cmpuint (rows, >=, 13);
	g_assert_cmpuint (rows, <=, 15);

	/* Only shown items were asked for their children */
	g_assert_cmpuint (log.queries, <=, rows);
	g_assert_nonnull (lrg_tree_view_get_row_widget (tree, 0));
	g_assert_null (lrg_tree_view_get_row_widget (tree, 500));

	lrg_tree_view_scroll_to (tree, 9000, FALSE);
	lrg_canvas_record (canvas);
	g_assert_nonnull (lrg_tree_view_get_row_widget (tree, 9000));
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (lrg_tree_view_get_row_widget (tree, 9000))), ==, "root-9000");
	/* Mid-tree needs one more buffer row than at the top; never more */
	g_assert_cmpuint (lrg_tree_view_get_n_rows (tree), <=, rows + 1);
	g_assert_cmpuint (log.queries, <=, rows * 2 + 2);
}

static void
on_object (gpointer  instance,
           GObject  *item,
           gpointer  user_data)
{
	*(GObject **) user_data = item;
}

static void
test_tree_expand (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	TreeLog              log = { 0 };
	LrgTreeView         *tree = tree_fixture (canvas, 20, &log);
	GObject             *root0 = lrg_tree_view_get_item (tree, 0);
	GObject             *expanded = NULL;
	GObject             *child0;

	g_signal_connect (tree, "row-expanded", G_CALLBACK (on_object), &expanded);

	/* Expand through the API: 5 children flattened under root-0 */
	lrg_tree_view_expand (tree, root0);
	g_assert_true (expanded == root0);
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree), ==, 25);
	g_assert_cmpstr (tree_name (tree, 1), ==, "root-0/0");
	g_assert_cmpuint (lrg_tree_view_get_depth (tree, 1), ==, 1);
	g_assert_cmpstr (tree_name (tree, 6), ==, "root-1");

	/* Grandchildren at depth 2 */
	child0 = lrg_tree_view_get_item (tree, 1);
	lrg_tree_view_expand (tree, child0);
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree), ==, 28);
	g_assert_cmpstr (tree_name (tree, 2), ==, "root-0/0/0");
	g_assert_cmpuint (lrg_tree_view_get_depth (tree, 2), ==, 2);

	/* Leaves do not expand */
	lrg_tree_view_expand (tree, lrg_tree_view_get_item (tree, 2));
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree), ==, 28);

	/* Collapse and re-expand: the sub-branch stays open */
	lrg_tree_view_collapse (tree, root0);
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree), ==, 20);
	g_assert_cmpuint (lrg_tree_view_get_position (tree, child0), ==, LRG_TREE_VIEW_INVALID_POSITION);
	lrg_tree_view_expand (tree, root0);
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree), ==, 28);
	lrg_tree_view_collapse_all (tree);
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree), ==, 20);
	settle (canvas, 300);

	/* A click on the arrow column of root-1 expands it; the arrow
	 * turns with a spring */
	click_at (canvas, 15.0f, tree_row_y (1));
	g_assert_true (lrg_tree_view_is_expanded (tree, lrg_tree_view_get_item (tree, 1)));
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree), ==, 25);
	g_assert_true (lrg_canvas_update (canvas, DT));
	settle (canvas, 300);

	/* A click on the label selects without toggling */
	click_at (canvas, 100.0f, tree_row_y (3));
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_selected_item (tree))->name, ==, "root-1/1");
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree), ==, 25);
}

static void
test_tree_keyboard (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	TreeLog              log = { 0 };
	LrgTreeView         *tree = tree_fixture (canvas, 20, &log);
	GObject             *activated = NULL;

	g_signal_connect (tree, "row-activated", G_CALLBACK (on_object), &activated);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (tree));

	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_cursor_item (tree))->name, ==, "root-0");

	/* Right expands, Right again steps into the first child */
	key (canvas, GRL_KEY_RIGHT);
	g_assert_true (lrg_tree_view_is_expanded (tree, lrg_tree_view_get_item (tree, 0)));
	key (canvas, GRL_KEY_RIGHT);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_cursor_item (tree))->name, ==, "root-0/0");
	key (canvas, GRL_KEY_DOWN);
	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_cursor_item (tree))->name, ==, "root-0/2");
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_selected_item (tree))->name, ==, "root-0/2");

	/* Right on a leaf does nothing (but keeps focus) */
	key (canvas, GRL_KEY_RIGHT);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_cursor_item (tree))->name, ==, "root-0/2");
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (tree));

	/* Left goes to the parent, Left again collapses it */
	key (canvas, GRL_KEY_LEFT);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_cursor_item (tree))->name, ==, "root-0");
	key (canvas, GRL_KEY_LEFT);
	g_assert_false (lrg_tree_view_is_expanded (tree, lrg_tree_view_get_item (tree, 0)));
	g_assert_cmpuint (lrg_tree_view_get_n_visible (tree), ==, 20);

	/* Collapsing hides the cursor's branch: the cursor moves up */
	key (canvas, GRL_KEY_RIGHT);
	key (canvas, GRL_KEY_RIGHT);
	lrg_tree_view_collapse (tree, lrg_tree_view_get_item (tree, 0));
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_cursor_item (tree))->name, ==, "root-0");

	key (canvas, GRL_KEY_END);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_cursor_item (tree))->name, ==, "root-19");
	activate (canvas);
	g_assert_nonnull (activated);
	g_assert_cmpstr (TEST_ITEM (activated)->name, ==, "root-19");

	/* Double click activates too */
	activated = NULL;
	settle (canvas, 300);
	lrg_tree_view_scroll_to (tree, 0, FALSE);
	click_at (canvas, 100.0f, tree_row_y (2));
	click_at (canvas, 100.0f, tree_row_y (2));
	g_assert_nonnull (activated);
	g_assert_cmpstr (TEST_ITEM (activated)->name, ==, "root-2");
}

/* ==========================================================================
 * Tree view: editor features (v0.8.1)
 * ========================================================================== */

/* key_mods: one frame with a key press while @mods are held */
static void
key_mods (LrgCanvas         *canvas,
          GrlKey             k,
          LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	frame_pointer (&input, -10.0f, -10.0f);
	input.modifiers = mods;
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

/* type_char: one frame typing @c */
static void
type_char (LrgCanvas *canvas,
           gunichar   c)
{
	LrgCanvasInput input;

	frame_pointer (&input, -10.0f, -10.0f);
	lrg_canvas_input_add_char (&input, c);
	lrg_canvas_process_input (canvas, &input);
}

/* click_mods: a left click at (@x, @y) with @mods held */
static void
click_mods (LrgCanvas         *canvas,
            gfloat             x,
            gfloat             y,
            LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	move_to (canvas, x, y);
	frame_pointer (&input, x, y);
	input.modifiers = mods;
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
	frame_pointer (&input, x, y);
	input.modifiers = mods;
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

/* selected_names: the selection as "a,b,c" in get_selected_items order */
static gchar *
selected_names (LrgTreeView *tree)
{
	g_autoptr(GPtrArray) items = lrg_tree_view_get_selected_items (tree);
	GString             *out = g_string_new (NULL);
	guint                i;

	for (i = 0; i < items->len; i++)
	{
		if (i > 0)
			g_string_append_c (out, ',');
		g_string_append (out, TEST_ITEM (g_ptr_array_index (items, i))->name);
	}
	return g_string_free (out, FALSE);
}

#define ASSERT_SELECTION(tree, expected) \
	G_STMT_START { \
		g_autofree gchar *got_ = selected_names (tree); \
		g_assert_cmpstr (got_, ==, expected); \
	} G_STMT_END

static void
test_tree_multi_select (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	TreeLog              log = { 0 };
	LrgTreeView         *tree = tree_fixture (canvas, 20, &log);
	guint                changes = 0;
	GObject             *root0 = lrg_tree_view_get_item (tree, 0);

	lrg_tree_view_set_selection_mode (tree, LRG_SELECTION_MODE_MULTIPLE);
	g_signal_connect_swapped (tree, "selection-changed", G_CALLBACK (on_count_swapped), &changes);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (tree));

	/* Click replaces; clicking it again changes nothing */
	click_at (canvas, 100.0f, tree_row_y (2));
	ASSERT_SELECTION (tree, "root-2");
	g_assert_cmpuint (changes, ==, 1);
	settle (canvas, 300);
	click_mods (canvas, 100.0f, tree_row_y (2), LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpuint (changes, ==, 1);
	settle (canvas, 300);

	/* Ctrl toggles and moves the anchor; Shift extends from it */
	click_mods (canvas, 100.0f, tree_row_y (5), LRG_INPUT_MODIFIER_CTRL);
	ASSERT_SELECTION (tree, "root-2,root-5");
	g_assert_cmpuint (changes, ==, 2);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_anchor_item (tree))->name, ==, "root-5");
	settle (canvas, 300);
	click_mods (canvas, 100.0f, tree_row_y (7), LRG_INPUT_MODIFIER_SHIFT);
	ASSERT_SELECTION (tree, "root-5,root-6,root-7");
	g_assert_cmpuint (changes, ==, 3);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_anchor_item (tree))->name, ==, "root-5");
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_cursor_item (tree))->name, ==, "root-7");

	/* Shift+arrows extend the range from the anchor, both ways */
	key_mods (canvas, GRL_KEY_UP, LRG_INPUT_MODIFIER_SHIFT);
	ASSERT_SELECTION (tree, "root-5,root-6");
	key_mods (canvas, GRL_KEY_UP, LRG_INPUT_MODIFIER_SHIFT);
	key_mods (canvas, GRL_KEY_UP, LRG_INPUT_MODIFIER_SHIFT);
	ASSERT_SELECTION (tree, "root-4,root-5");
	g_assert_cmpuint (changes, ==, 6);
	/* A plain arrow collapses back to one row */
	key (canvas, GRL_KEY_DOWN);
	ASSERT_SELECTION (tree, "root-5");

	/* The API: TOGGLE then EXTEND from the new anchor */
	lrg_tree_view_select_item_full (tree, root0, LRG_SELECT_MODE_TOGGLE);
	ASSERT_SELECTION (tree, "root-0,root-5");
	lrg_tree_view_select_item_full (tree, lrg_tree_view_get_item (tree, 2), LRG_SELECT_MODE_EXTEND);
	ASSERT_SELECTION (tree, "root-0,root-1,root-2");
	g_assert_true (lrg_tree_view_get_anchor_item (tree) == root0);
	g_assert_cmpuint (lrg_tree_view_get_n_selected (tree), ==, 3);

	/* Hidden selected items stay selected and come last */
	lrg_tree_view_expand (tree, root0);
	lrg_tree_view_select_item_full (tree, lrg_tree_view_get_item (tree, 2), LRG_SELECT_MODE_TOGGLE);
	ASSERT_SELECTION (tree, "root-0,root-0/1,root-1,root-2");
	lrg_tree_view_collapse (tree, root0);
	ASSERT_SELECTION (tree, "root-0,root-1,root-2,root-0/1");

	/* Single mode: one item; TOGGLE on it clears, EXTEND replaces */
	changes = 0;
	lrg_tree_view_set_selection_mode (tree, LRG_SELECTION_MODE_SINGLE);
	g_assert_cmpuint (lrg_tree_view_get_n_selected (tree), ==, 1);
	lrg_tree_view_select_item_full (tree, lrg_tree_view_get_item (tree, 3), LRG_SELECT_MODE_EXTEND);
	ASSERT_SELECTION (tree, "root-3");
	lrg_tree_view_select_item_full (tree, lrg_tree_view_get_item (tree, 3), LRG_SELECT_MODE_TOGGLE);
	ASSERT_SELECTION (tree, "");
	g_assert_cmpuint (changes, ==, 3);

	/* None mode selects nothing but moves the cursor */
	lrg_tree_view_set_selection_mode (tree, LRG_SELECTION_MODE_NONE);
	lrg_tree_view_select_item_full (tree, lrg_tree_view_get_item (tree, 4), LRG_SELECT_MODE_REPLACE);
	g_assert_cmpuint (lrg_tree_view_get_n_selected (tree), ==, 0);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_cursor_item (tree))->name, ==, "root-4");
}

/* Children function that also records child -> parent links */
typedef struct
{
	TreeLog     log;
	GHashTable *parents;   /* GObject * -> GObject * (not owned) */
	guint       asked;
} LinkedTree;

static GListModel *
linked_children (LrgTreeView *tree,
                 GObject     *item,
                 gpointer     user_data)
{
	LinkedTree *lt = user_data;
	GListModel *children = tree_children (tree, item, &lt->log);
	guint       i;

	for (i = 0; children != NULL && i < g_list_model_get_n_items (children); i++)
	{
		g_autoptr(GObject) child = g_list_model_get_item (children, i);

		g_hash_table_insert (lt->parents, child, item);
	}
	return children;
}

static GObject *
linked_parent (LrgTreeView *tree,
               GObject     *item,
               gpointer     user_data)
{
	LinkedTree *lt = user_data;

	lt->asked++;
	return g_hash_table_lookup (lt->parents, item);
}

static void
test_tree_reveal (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	TreeLog              log = { 0 };
	LrgTreeView         *tree = tree_fixture (canvas, 50, &log);
	GObject             *root30 = lrg_tree_view_get_item (tree, 30);
	GObject             *child;
	GObject             *grand;
	guint                expanded = 0;
	gfloat               ry;
	gfloat               rh;

	g_signal_connect_swapped (tree, "row-expanded", G_CALLBACK (on_count_swapped), &expanded);

	/* Get hold of root-30/0/2, then hide it again */
	lrg_tree_view_expand (tree, root30);
	child = lrg_tree_view_get_item (tree, 31);
	lrg_tree_view_expand (tree, child);
	grand = lrg_tree_view_get_item (tree, 34);
	g_assert_cmpstr (TEST_ITEM (grand)->name, ==, "root-30/0/2");
	lrg_tree_view_collapse_all (tree);
	settle (canvas, 300);
	expanded = 0;
	g_assert_false (lrg_tree_view_get_row_rect (tree, grand, NULL, NULL, NULL, NULL));

	/* No parent function: found by search, ancestors expanded root first */
	g_assert_true (lrg_tree_view_reveal (tree, grand));
	g_assert_cmpuint (expanded, ==, 2);
	g_assert_true (lrg_tree_view_is_expanded (tree, root30));
	g_assert_true (lrg_tree_view_is_expanded (tree, child));
	g_assert_cmpuint (lrg_tree_view_get_position (tree, grand), ==, 34);

	/* The scroll glides (spring), then the row is fully in view */
	g_assert_true (lrg_canvas_update (canvas, DT));
	settle (canvas, 600);
	g_assert_cmpfloat (lrg_tree_view_get_scroll_offset (tree), >, 0.0f);
	g_assert_true (lrg_tree_view_get_row_rect (tree, grand, NULL, &ry, NULL, &rh));
	g_assert_cmpfloat (ry, >=, 4.0f - 0.01f);
	g_assert_cmpfloat (ry + rh, <=, 296.0f + 0.01f);
	g_assert_null (lrg_tree_view_get_selected_item (tree));

	/* Revealing a visible item only scrolls; an unknown item fails */
	expanded = 0;
	g_assert_true (lrg_tree_view_reveal (tree, root30));
	g_assert_cmpuint (expanded, ==, 0);
	{
		g_autoptr(TestItem) stray = item_new ("stray", 0);

		g_assert_false (lrg_tree_view_reveal (tree, G_OBJECT (stray)));
	}
}

static void
test_tree_reveal_parent_func (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LinkedTree           lt = { { 0 }, NULL, 0 };
	LrgTreeView         *tree;
	GObject             *root40;
	GObject             *leaf;

	lt.parents = g_hash_table_new (g_direct_hash, g_direct_equal);
	tree = tree_fixture (canvas, 50, &lt.log);
	lrg_tree_view_set_children_func (tree, linked_children, &lt, NULL);
	lrg_tree_view_set_parent_func (tree, linked_parent, &lt, NULL);

	root40 = lrg_tree_view_get_item (tree, 40);
	lrg_tree_view_expand (tree, root40);
	leaf = lrg_tree_view_get_item (tree, 44);
	g_assert_cmpstr (TEST_ITEM (leaf)->name, ==, "root-40/3");
	lrg_tree_view_collapse (tree, root40);

	g_assert_true (lrg_tree_view_reveal (tree, leaf));
	g_assert_cmpuint (lt.asked, ==, 2);   /* leaf -> root-40 -> NULL */
	g_assert_true (lrg_tree_view_is_expanded (tree, root40));
	settle (canvas, 600);
	g_assert_nonnull (lrg_tree_view_get_row_widget (tree, 44));

	g_hash_table_unref (lt.parents);
}

typedef struct
{
	guint    labels;
	guint    cancelled;
	gboolean accept;
	gchar   *text;
	GObject *item;
} EditLog;

static gboolean
on_edit_label (LrgTreeView *tree,
               GObject     *item,
               const gchar *text,
               EditLog     *log)
{
	log->labels++;
	log->item = item;
	g_free (log->text);
	log->text = g_strdup (text);
	return log->accept;
}

static void
on_edit_cancelled (LrgTreeView *tree,
                   GObject     *item,
                   EditLog     *log)
{
	log->cancelled++;
	log->item = item;
}

static void
test_tree_rename (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	TreeLog              log = { 0 };
	LrgTreeView         *tree = tree_fixture (canvas, 20, &log);
	EditLog              ed = { 0 };
	LrgWidget           *editor;
	gfloat               rx;
	gfloat               ry;
	gfloat               rw;
	gfloat               rh;

	g_signal_connect (tree, "edit-label", G_CALLBACK (on_edit_label), &ed);
	g_signal_connect (tree, "edit-cancelled", G_CALLBACK (on_edit_cancelled), &ed);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (tree));

	/* F2 on the cursor row opens the editor over its label */
	click_at (canvas, 100.0f, tree_row_y (1));
	settle (canvas, 300);
	key (canvas, GRL_KEY_F2);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_editing_item (tree))->name, ==, "root-1");
	editor = lrg_tree_view_get_editor (tree);
	g_assert_nonnull (editor);
	g_assert_true (LRG_IS_TEXT_INPUT (editor));
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (editor)), ==, "root-1");
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == editor);
	g_assert_true (lrg_tree_view_get_row_rect (tree, lrg_tree_view_get_item (tree, 1), &rx, &ry, &rw, &rh));
	g_assert_cmpfloat (lrg_widget_get_world_y (editor), >=, ry);
	g_assert_cmpfloat (lrg_widget_get_world_y (editor) + lrg_widget_get_height (editor), <=, ry + rh + 0.01f);
	g_assert_cmpfloat (lrg_widget_get_world_x (editor), >, rx);
	lrg_canvas_record (canvas);

	/* Typing replaces the selected text; a refused name keeps editing */
	type_char (canvas, 'x');
	type_char (canvas, 'y');
	g_assert_cmpstr (lrg_text_input_get_text (LRG_TEXT_INPUT (editor)), ==, "xy");
	key (canvas, GRL_KEY_UP);   /* arrows stay with the input */
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_cursor_item (tree))->name, ==, "root-1");
	key (canvas, GRL_KEY_ENTER);
	g_assert_cmpuint (ed.labels, ==, 1);
	g_assert_cmpstr (ed.text, ==, "xy");
	g_assert_nonnull (lrg_tree_view_get_editing_item (tree));

	/* Accepted: the editor goes and the view has the focus again */
	ed.accept = TRUE;
	key (canvas, GRL_KEY_ENTER);
	g_assert_cmpuint (ed.labels, ==, 2);
	g_assert_null (lrg_tree_view_get_editing_item (tree));
	g_assert_null (lrg_tree_view_get_editor (tree));
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (tree));
	g_assert_cmpuint (ed.cancelled, ==, 0);

	/* Escape cancels */
	g_assert_true (lrg_tree_view_start_editing (tree, lrg_tree_view_get_item (tree, 2)));
	key (canvas, GRL_KEY_ESCAPE);
	g_assert_cmpuint (ed.cancelled, ==, 1);
	g_assert_cmpstr (TEST_ITEM (ed.item)->name, ==, "root-2");
	g_assert_null (lrg_tree_view_get_editing_item (tree));
	g_assert_cmpuint (ed.labels, ==, 2);

	/* Clicking another row moves the focus: the edit is committed */
	g_assert_true (lrg_tree_view_start_editing (tree, lrg_tree_view_get_item (tree, 3)));
	type_char (canvas, 'z');
	settle (canvas, 300);
	click_at (canvas, 100.0f, tree_row_y (6));
	g_assert_cmpuint (ed.labels, ==, 3);
	g_assert_cmpstr (ed.text, ==, "z");
	g_assert_cmpstr (TEST_ITEM (ed.item)->name, ==, "root-3");
	g_assert_null (lrg_tree_view_get_editing_item (tree));

	/* Collapsing the edited row's parent cancels; so does a new model */
	lrg_tree_view_expand (tree, lrg_tree_view_get_item (tree, 0));
	g_assert_true (lrg_tree_view_start_editing (tree, lrg_tree_view_get_item (tree, 2)));
	lrg_tree_view_collapse (tree, lrg_tree_view_get_item (tree, 0));
	g_assert_cmpuint (ed.cancelled, ==, 2);
	g_assert_null (lrg_tree_view_get_editing_item (tree));
	g_assert_true (lrg_tree_view_start_editing (tree, lrg_tree_view_get_item (tree, 4)));
	lrg_tree_view_stop_editing (tree, FALSE);
	g_assert_cmpuint (ed.labels, ==, 4);
	g_assert_null (lrg_tree_view_get_editing_item (tree));

	/* Items without a row cannot be edited */
	{
		g_autoptr(TestItem) stray = item_new ("stray", 0);

		g_assert_false (lrg_tree_view_start_editing (tree, G_OBJECT (stray)));
	}
	g_free (ed.text);
}

static void
test_tree_drop_geometry (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	TreeLog              log = { 0 };
	LrgTreeView         *tree = tree_fixture (canvas, 20, &log);
	GObject             *root0 = lrg_tree_view_get_item (tree, 0);
	GObject             *item = NULL;
	LrgDropPosition      pos;
	LrgPaintList        *list;
	guint                base;
	gfloat               x;
	gfloat               y;
	gfloat               w;
	gfloat               h;

	lrg_tree_view_expand (tree, root0);
	settle (canvas, 300);

	/* Row rects and hit tests, canvas coordinates */
	g_assert_true (lrg_tree_view_get_row_rect (tree, root0, &x, &y, &w, &h));
	g_assert_cmpfloat (x, ==, 4.0f);
	g_assert_cmpfloat (y, ==, 4.0f);
	g_assert_cmpfloat (w, ==, 192.0f);
	g_assert_cmpfloat (h, ==, 24.0f);
	g_assert_true (lrg_tree_view_get_row_rect (tree, lrg_tree_view_get_item (tree, 3), NULL, &y, NULL, NULL));
	g_assert_cmpfloat (y, ==, 4.0f + 3.0f * 24.0f);
	g_assert_cmpstr (TEST_ITEM (lrg_tree_view_get_item_at (tree, 100.0f, tree_row_y (1)))->name, ==, "root-0/0");
	g_assert_null (lrg_tree_view_get_item_at (tree, 250.0f, tree_row_y (1)));
	g_assert_null (lrg_tree_view_get_item_at (tree, 100.0f, 299.0f));

	/* Quarters: before / into / after; after an open parent = before its first child */
	g_assert_true (lrg_tree_view_get_drop_position (tree, 4.0f + 2.0f, &item, &pos));
	g_assert_true (item == root0);
	g_assert_cmpint (pos, ==, LRG_DROP_POSITION_BEFORE);
	g_assert_true (lrg_tree_view_get_drop_position (tree, 4.0f + 12.0f, &item, &pos));
	g_assert_true (item == root0);
	g_assert_cmpint (pos, ==, LRG_DROP_POSITION_INTO);
	g_assert_true (lrg_tree_view_get_drop_position (tree, 4.0f + 22.0f, &item, &pos));
	g_assert_cmpstr (TEST_ITEM (item)->name, ==, "root-0/0");
	g_assert_cmpint (pos, ==, LRG_DROP_POSITION_BEFORE);
	g_assert_true (lrg_tree_view_get_drop_position (tree, 4.0f + 24.0f * 2.0f + 22.0f, &item, &pos));
	g_assert_cmpstr (TEST_ITEM (item)->name, ==, "root-0/1");
	g_assert_cmpint (pos, ==, LRG_DROP_POSITION_AFTER);
	g_assert_false (lrg_tree_view_get_drop_position (tree, -5.0f, &item, &pos));
	g_assert_null (item);

	/* Painting: an INTO highlight, a line that glides, then nothing */
	list = lrg_canvas_record (canvas);
	base = lrg_paint_list_get_n_instances (list);
	lrg_tree_view_set_drop_indicator (tree, lrg_tree_view_get_item (tree, 7), LRG_DROP_POSITION_INTO);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, base);
	lrg_tree_view_set_drop_indicator (tree, lrg_tree_view_get_item (tree, 7), LRG_DROP_POSITION_AFTER);
	g_assert_true (lrg_tree_view_get_drop_indicator (tree, &pos) == lrg_tree_view_get_item (tree, 7));
	g_assert_cmpint (pos, ==, LRG_DROP_POSITION_AFTER);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, base);
	settle (canvas, 300);
	lrg_tree_view_set_drop_indicator (tree, lrg_tree_view_get_item (tree, 10), LRG_DROP_POSITION_BEFORE);
	g_assert_true (lrg_canvas_update (canvas, DT));
	settle (canvas, 300);
	lrg_tree_view_clear_drop_indicator (tree);
	g_assert_null (lrg_tree_view_get_drop_indicator (tree, NULL));
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, base);

	/* Below the last row: after the last root item, even when it is
	 * open; an empty tree drops into the root */
	{
		g_autoptr(LrgCanvas) c2 = canvas_new ();
		TreeLog              l2 = { 0 };
		LrgTreeView         *small = tree_fixture (c2, 3, &l2);
		g_autoptr(GListStore) empty = g_list_store_new (TEST_TYPE_ITEM);

		lrg_tree_view_expand (small, lrg_tree_view_get_item (small, 2));
		g_assert_true (lrg_tree_view_get_drop_position (small, 250.0f, &item, &pos));
		g_assert_cmpstr (TEST_ITEM (item)->name, ==, "root-2");
		g_assert_cmpint (pos, ==, LRG_DROP_POSITION_AFTER);
		lrg_tree_view_set_model (small, G_LIST_MODEL (empty));
		g_assert_true (lrg_tree_view_get_drop_position (small, 100.0f, &item, &pos));
		g_assert_null (item);
		g_assert_cmpint (pos, ==, LRG_DROP_POSITION_INTO);
	}

	g_assert_cmpstr (g_enum_get_value (g_type_class_peek (LRG_TYPE_DROP_POSITION) != NULL ?
	                                   g_type_class_peek (LRG_TYPE_DROP_POSITION) :
	                                   g_type_class_ref (LRG_TYPE_DROP_POSITION),
	                                   LRG_DROP_POSITION_AFTER)->value_nick, ==, "after");
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_SELECT_MODE));
}

/* ==========================================================================
 * Tile view
 *
 * 300 x 300 at (0, 0), padding 2, tiles 64 x 64, spacing 8: four
 * columns, the grid centred from x = 10, first row at y = 8.
 * ========================================================================== */

static LrgTileView *
tile_fixture (LrgCanvas *canvas,
              guint      n)
{
	LrgTileView          *tiles = lrg_tile_view_new ();
	g_autoptr(GListStore) store = store_new (n);

	lrg_tile_view_set_tile_size (tiles, 64.0f, 64.0f);
	lrg_tile_view_set_tile_spacing (tiles, 8.0f);
	lrg_tile_view_set_model (tiles, G_LIST_MODEL (store));
	place (canvas, tiles, 0.0f, 0.0f, 300.0f, 300.0f);
	return tiles;
}

static void
test_tiles_virtualized (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTileView         *tiles = tile_fixture (canvas, 10000);
	guint                n;

	g_assert_cmpuint (lrg_tile_view_get_columns (tiles), ==, 4);
	n = lrg_tile_view_get_n_tiles (tiles);
	g_assert_cmpuint (n, >=, 16);
	g_assert_cmpuint (n, <=, 32);
	g_assert_nonnull (lrg_tile_view_get_tile_widget (tiles, 0));
	g_assert_null (lrg_tile_view_get_tile_widget (tiles, 400));

	lrg_tile_view_scroll_to (tiles, 9999, FALSE);
	lrg_canvas_record (canvas);
	g_assert_nonnull (lrg_tile_view_get_tile_widget (tiles, 9999));
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (lrg_tile_view_get_tile_widget (tiles, 9999))), ==, "item-9999");
	g_assert_null (lrg_tile_view_get_tile_widget (tiles, 0));
	g_assert_cmpuint (lrg_tile_view_get_n_tiles (tiles), <=, 32);
}

static void
test_tiles_keyboard (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	g_autoptr(GListStore) small = store_new (10);
	LrgTileView          *tiles = tile_fixture (canvas, 1000);
	guint                 activated = G_MAXUINT;

	g_signal_connect (tiles, "tile-activated", G_CALLBACK (on_activated), &activated);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (tiles));

	key (canvas, GRL_KEY_RIGHT);
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 0);
	key (canvas, GRL_KEY_RIGHT);
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 1);
	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 5);
	key (canvas, GRL_KEY_LEFT);
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 4);
	key (canvas, GRL_KEY_UP);
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 0);
	g_assert_cmpuint (lrg_tile_view_get_selected (tiles), ==, 0);

	/* Edges keep the cursor and the focus */
	key (canvas, GRL_KEY_UP);
	key (canvas, GRL_KEY_LEFT);
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 0);
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (tiles));

	key (canvas, GRL_KEY_PAGE_DOWN);
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 16);
	key (canvas, GRL_KEY_END);
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 999);
	settle (canvas, 300);
	g_assert_nonnull (lrg_tile_view_get_tile_widget (tiles, 999));
	activate (canvas);
	g_assert_cmpuint (activated, ==, 999);

	/* Short last row: Down from the row above lands on the last tile */
	lrg_tile_view_set_model (tiles, G_LIST_MODEL (small));
	lrg_canvas_record (canvas);
	lrg_tile_view_select (tiles, 6);
	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 9);
	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 9);
}

static void
test_tiles_hover_click (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTileView         *tiles = tile_fixture (canvas, 50);
	guint                activated = G_MAXUINT;

	g_signal_connect (tiles, "tile-activated", G_CALLBACK (on_activated), &activated);
	settle (canvas, 300);

	/* Hovering lifts a tile with a spring */
	move_to (canvas, 40.0f, 40.0f);
	g_assert_true (lrg_canvas_update (canvas, DT));
	settle (canvas, 300);

	/* Tile 1 is at x 82..146; the gap between tiles misses */
	click_at (canvas, 100.0f, 40.0f);
	g_assert_true (lrg_tile_view_is_selected (tiles, 1));
	click_at (canvas, 78.0f, 40.0f);
	g_assert_true (lrg_tile_view_is_selected (tiles, 1));
	click_at (canvas, 100.0f, 40.0f);
	click_at (canvas, 100.0f, 40.0f);
	g_assert_cmpuint (activated, ==, 1);
	click_at (canvas, 40.0f, 110.0f);
	g_assert_cmpuint (lrg_tile_view_get_selected (tiles), ==, 4);
}

/* ==========================================================================
 * Tile view: editor features (v0.8.1)
 * ========================================================================== */

/* positions_str: a GArray of guint as "a,b,c" (frees the array) */
static gchar *
positions_str (GArray *positions)
{
	GString *out = g_string_new (NULL);
	guint    i;

	for (i = 0; i < positions->len; i++)
		g_string_append_printf (out, "%s%u", i > 0 ? "," : "", g_array_index (positions, guint, i));
	g_array_unref (positions);
	return g_string_free (out, FALSE);
}

#define ASSERT_POSITIONS(array, expected) \
	G_STMT_START { \
		g_autofree gchar *got_ = positions_str (array); \
		g_assert_cmpstr (got_, ==, expected); \
	} G_STMT_END

static void
test_tiles_geometry_multi (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgTileView         *tiles = tile_fixture (canvas, 50);
	gfloat               x;
	gfloat               y;
	gfloat               w;
	gfloat               h;
	guint                changes = 0;

	settle (canvas, 300);
	g_signal_connect_swapped (tiles, "selection-changed", G_CALLBACK (on_count_swapped), &changes);

	/* Rects and hit tests in canvas coordinates; gaps miss */
	g_assert_true (lrg_tile_view_get_tile_rect (tiles, 1, &x, &y, &w, &h));
	g_assert_cmpfloat (x, ==, 82.0f);
	g_assert_cmpfloat (y, ==, 8.0f);
	g_assert_cmpfloat (w, ==, 64.0f);
	g_assert_cmpfloat (h, ==, 64.0f);
	g_assert_true (lrg_tile_view_get_tile_rect (tiles, 5, &x, &y, NULL, NULL));
	g_assert_cmpfloat (x, ==, 82.0f);
	g_assert_cmpfloat (y, ==, 80.0f);
	g_assert_false (lrg_tile_view_get_tile_rect (tiles, 50, NULL, NULL, NULL, NULL));
	g_assert_cmpuint (lrg_tile_view_get_item_at (tiles, 100.0f, 40.0f), ==, 1);
	g_assert_cmpuint (lrg_tile_view_get_item_at (tiles, 78.0f, 40.0f), ==, LRG_TILE_VIEW_INVALID_POSITION);
	g_assert_cmpuint (lrg_tile_view_get_item_at (tiles, 40.0f, 110.0f), ==, 4);
	g_assert_cmpuint (lrg_tile_view_get_item_at (tiles, 400.0f, 40.0f), ==, LRG_TILE_VIEW_INVALID_POSITION);

	/* Clicks: Shift extends from the anchor, Ctrl toggles */
	lrg_tile_view_set_selection_mode (tiles, LRG_SELECTION_MODE_MULTIPLE);
	click_at (canvas, 40.0f, 40.0f);
	settle (canvas, 300);
	click_mods (canvas, x + 10.0f, y + 10.0f, LRG_INPUT_MODIFIER_SHIFT);
	ASSERT_POSITIONS (lrg_tile_view_get_selected_positions (tiles), "0,1,2,3,4,5");
	g_assert_cmpuint (lrg_tile_view_get_anchor (tiles), ==, 0);
	settle (canvas, 300);
	lrg_tile_view_get_tile_rect (tiles, 9, &x, &y, NULL, NULL);
	click_mods (canvas, x + 10.0f, y + 10.0f, LRG_INPUT_MODIFIER_CTRL);
	ASSERT_POSITIONS (lrg_tile_view_get_selected_positions (tiles), "0,1,2,3,4,5,9");
	g_assert_cmpuint (lrg_tile_view_get_anchor (tiles), ==, 9);

	/* Shift+arrows extend from the anchor */
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (tiles));
	key_mods (canvas, GRL_KEY_RIGHT, LRG_INPUT_MODIFIER_SHIFT);
	key_mods (canvas, GRL_KEY_DOWN, LRG_INPUT_MODIFIER_SHIFT);
	ASSERT_POSITIONS (lrg_tile_view_get_selected_positions (tiles), "9,10,11,12,13,14");
	key (canvas, GRL_KEY_LEFT);
	ASSERT_POSITIONS (lrg_tile_view_get_selected_positions (tiles), "13");

	/* The API, then single mode */
	lrg_tile_view_select_full (tiles, 20, LRG_SELECT_MODE_TOGGLE);
	lrg_tile_view_select_full (tiles, 22, LRG_SELECT_MODE_EXTEND);
	ASSERT_POSITIONS (lrg_tile_view_get_selected_positions (tiles), "20,21,22");
	g_assert_cmpuint (lrg_tile_view_get_cursor (tiles), ==, 22);
	lrg_tile_view_set_selection_mode (tiles, LRG_SELECTION_MODE_SINGLE);
	lrg_tile_view_select_full (tiles, 30, LRG_SELECT_MODE_EXTEND);
	ASSERT_POSITIONS (lrg_tile_view_get_selected_positions (tiles), "30");
	lrg_tile_view_select_full (tiles, 30, LRG_SELECT_MODE_TOGGLE);
	ASSERT_POSITIONS (lrg_tile_view_get_selected_positions (tiles), "");
	g_assert_cmpuint (changes, >, 0);
}

/* ==========================================================================
 * List view: editor features (v0.8.1)
 *
 * 200 x 300 at (0, 0), padding 4, rows 24: row p spans
 * y = 4 + 24 p .. 28 + 24 p.
 * ========================================================================== */

static LrgListView *
list_fixture (LrgCanvas  *canvas,
              GListStore *store)
{
	LrgListView *list = lrg_list_view_new ();

	lrg_list_view_set_row_height (list, 24.0f);
	lrg_list_view_set_model (list, G_LIST_MODEL (store));
	place (canvas, list, 0.0f, 0.0f, 200.0f, 300.0f);
	return list;
}

static void
test_list_multi_select (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	g_autoptr(GListStore) store = store_new (40);
	LrgListView          *list = list_fixture (canvas, store);

	lrg_list_view_set_selection_mode (list, LRG_SELECTION_MODE_MULTIPLE);

	lrg_list_view_select_full (list, 2, LRG_SELECT_MODE_REPLACE);
	lrg_list_view_select_full (list, 5, LRG_SELECT_MODE_TOGGLE);
	ASSERT_POSITIONS (lrg_list_view_get_selected_positions (list), "2,5");
	g_assert_cmpuint (lrg_list_view_get_anchor (list), ==, 5);
	lrg_list_view_select_full (list, 7, LRG_SELECT_MODE_EXTEND);
	ASSERT_POSITIONS (lrg_list_view_get_selected_positions (list), "5,6,7");
	g_assert_cmpuint (lrg_list_view_get_cursor (list), ==, 7);

	/* Clicks map to the same modes */
	click_mods (canvas, 100.0f, 4.0f + 24.0f * 3.0f + 12.0f, LRG_INPUT_MODIFIER_SHIFT);
	ASSERT_POSITIONS (lrg_list_view_get_selected_positions (list), "3,4,5");
	settle (canvas, 300);
	click_mods (canvas, 100.0f, 4.0f + 24.0f * 9.0f + 12.0f, LRG_INPUT_MODIFIER_CTRL);
	ASSERT_POSITIONS (lrg_list_view_get_selected_positions (list), "3,4,5,9");

	/* Single mode */
	lrg_list_view_set_selection_mode (list, LRG_SELECTION_MODE_SINGLE);
	lrg_list_view_select_full (list, 11, LRG_SELECT_MODE_EXTEND);
	ASSERT_POSITIONS (lrg_list_view_get_selected_positions (list), "11");
	lrg_list_view_select_full (list, 11, LRG_SELECT_MODE_TOGGLE);
	ASSERT_POSITIONS (lrg_list_view_get_selected_positions (list), "");
}

static void
test_list_drop_geometry (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	g_autoptr(GListStore) store = store_new (40);
	LrgListView          *list = list_fixture (canvas, store);
	LrgPaintList         *paint;
	LrgDropPosition       pos;
	guint                 row;
	guint                 base;
	gfloat                x;
	gfloat                y;
	gfloat                w;
	gfloat                h;

	settle (canvas, 300);
	g_assert_true (lrg_list_view_get_row_rect (list, 3, &x, &y, &w, &h));
	g_assert_cmpfloat (x, ==, 4.0f);
	g_assert_cmpfloat (y, ==, 4.0f + 72.0f);
	g_assert_cmpfloat (w, ==, 192.0f);
	g_assert_cmpfloat (h, ==, 24.0f);
	g_assert_false (lrg_list_view_get_row_rect (list, 40, NULL, NULL, NULL, NULL));
	g_assert_cmpuint (lrg_list_view_get_item_at (list, 100.0f, 4.0f + 48.0f + 12.0f), ==, 2);
	g_assert_cmpuint (lrg_list_view_get_item_at (list, 250.0f, 20.0f), ==, LRG_LIST_VIEW_INVALID_POSITION);

	/* Scrolled: rects follow the offset */
	lrg_list_view_scroll_to (list, 30, FALSE);
	lrg_canvas_record (canvas);
	g_assert_true (lrg_list_view_get_row_rect (list, 30, NULL, &y, NULL, NULL));
	g_assert_cmpuint (lrg_list_view_get_item_at (list, 100.0f, y + 12.0f), ==, 30);
	lrg_list_view_scroll_to (list, 0, FALSE);
	lrg_canvas_record (canvas);

	/* Quarters */
	g_assert_true (lrg_list_view_get_drop_position (list, 4.0f + 24.0f + 2.0f, &row, &pos));
	g_assert_cmpuint (row, ==, 1);
	g_assert_cmpint (pos, ==, LRG_DROP_POSITION_BEFORE);
	g_assert_true (lrg_list_view_get_drop_position (list, 4.0f + 24.0f + 12.0f, &row, &pos));
	g_assert_cmpint (pos, ==, LRG_DROP_POSITION_INTO);
	g_assert_true (lrg_list_view_get_drop_position (list, 4.0f + 24.0f + 22.0f, &row, &pos));
	g_assert_cmpuint (row, ==, 1);
	g_assert_cmpint (pos, ==, LRG_DROP_POSITION_AFTER);
	g_assert_false (lrg_list_view_get_drop_position (list, 400.0f, &row, &pos));

	/* Indicator: painted, glides, cleared by a model change */
	paint = lrg_canvas_record (canvas);
	base = lrg_paint_list_get_n_instances (paint);
	lrg_list_view_set_drop_indicator (list, 2, LRG_DROP_POSITION_AFTER);
	paint = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (paint), >, base);
	settle (canvas, 300);
	lrg_list_view_set_drop_indicator (list, 6, LRG_DROP_POSITION_BEFORE);
	g_assert_true (lrg_canvas_update (canvas, DT));
	settle (canvas, 300);
	g_assert_cmpuint (lrg_list_view_get_drop_indicator (list, &pos), ==, 6);
	g_assert_cmpint (pos, ==, LRG_DROP_POSITION_BEFORE);
	lrg_list_view_set_drop_indicator (list, 6, LRG_DROP_POSITION_INTO);
	paint = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (paint), >, base);
	{
		g_autoptr(TestItem) extra = item_new ("extra", 0);

		g_list_store_append (store, extra);
	}
	g_assert_cmpuint (lrg_list_view_get_drop_indicator (list, NULL), ==, LRG_LIST_VIEW_INVALID_POSITION);
	lrg_list_view_set_drop_indicator (list, 1, LRG_DROP_POSITION_BEFORE);
	lrg_list_view_clear_drop_indicator (list);
	paint = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (paint), ==, base);

	/* Below the last row, and an empty list */
	{
		g_autoptr(LrgCanvas)  c2 = canvas_new ();
		g_autoptr(GListStore) few = store_new (3);
		g_autoptr(GListStore) none = store_new (0);
		LrgListView          *small = list_fixture (c2, few);

		g_assert_true (lrg_list_view_get_drop_position (small, 250.0f, &row, &pos));
		g_assert_cmpuint (row, ==, 2);
		g_assert_cmpint (pos, ==, LRG_DROP_POSITION_AFTER);
		lrg_list_view_set_model (small, G_LIST_MODEL (none));
		g_assert_true (lrg_list_view_get_drop_position (small, 100.0f, &row, &pos));
		g_assert_cmpuint (row, ==, LRG_LIST_VIEW_INVALID_POSITION);
		g_assert_cmpint (pos, ==, LRG_DROP_POSITION_INTO);
	}
}

/* ==========================================================================
 * Splitter
 *
 * Horizontal, 400 x 200 at (0, 0), handle 8: 392 units for the panes,
 * the handle at x = 196 .. 204 at the default fraction.
 * ========================================================================== */

static LrgSplitter *
splitter_fixture (LrgCanvas  *canvas,
                  LrgWidget **start,
                  LrgWidget **end)
{
	LrgSplitter *split = lrg_splitter_new (LRG_ORIENTATION_HORIZONTAL);
	LrgWidget   *a = g_object_new (LRG_TYPE_WIDGET, NULL);
	LrgWidget   *b = g_object_new (LRG_TYPE_WIDGET, NULL);

	lrg_splitter_set_start_child (split, a);
	lrg_splitter_set_end_child (split, b);
	g_object_unref (a);
	g_object_unref (b);
	place (canvas, split, 0.0f, 0.0f, 400.0f, 200.0f);
	*start = a;
	*end = b;
	return split;
}

static void
test_splitter_drag (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *a;
	LrgWidget           *b;
	LrgSplitter         *split = splitter_fixture (canvas, &a, &b);
	guint                begins = 0;

	g_signal_connect_swapped (split, "drag-begin", G_CALLBACK (on_count_swapped), &begins);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 196.0f);
	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 196.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b), ==, 204.0f);
	g_assert_cmpfloat (lrg_widget_get_width (b), ==, 196.0f);

	/* Drag the handle 50 to the right; the grab offset is kept */
	move_to (canvas, 200.0f, 100.0f);
	press_at (canvas, 200.0f, 100.0f);
	g_assert_true (lrg_splitter_get_dragging (split));
	drag_to (canvas, 250.0f, 120.0f);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 246.0f);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 246.0f);
	release_at (canvas, 250.0f, 120.0f);
	g_assert_false (lrg_splitter_get_dragging (split));
	g_assert_cmpuint (begins, ==, 1);

	/* Proportional mode survives a resize */
	g_assert_cmpfloat_with_epsilon (lrg_splitter_get_fraction (split), 246.0f / 392.0f, 0.001f);
	lrg_widget_allocate (LRG_WIDGET (split), 0.0f, 0.0f, 792.0f, 200.0f);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 492.0f);
	lrg_widget_allocate (LRG_WIDGET (split), 0.0f, 0.0f, 400.0f, 200.0f);

	/* Minimum sizes clamp the drag at both ends */
	lrg_splitter_set_min_sizes (split, 100.0f, 120.0f);
	settle (canvas, 120);
	move_to (canvas, 250.0f, 100.0f);
	press_at (canvas, 250.0f, 100.0f);
	drag_to (canvas, 0.0f, 100.0f);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 100.0f);
	drag_to (canvas, 399.0f, 100.0f);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 392.0f - 120.0f);
	release_at (canvas, 399.0f, 100.0f);

	/* Absolute mode keeps the start pane's size on resize */
	lrg_splitter_set_position (split, 150.0f, FALSE);
	lrg_widget_allocate (LRG_WIDGET (split), 0.0f, 0.0f, 600.0f, 200.0f);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 150.0f);
	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 150.0f);
}

static void
test_splitter_keyboard_animate (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *a;
	LrgWidget           *b;
	LrgSplitter         *split = splitter_fixture (canvas, &a, &b);
	gfloat               mid;
	guint                frames;

	settle (canvas, 120);
	lrg_canvas_set_focused_widget (canvas, lrg_splitter_get_handle (split));

	/* Arrows move by 10 with a glide; Up / Down are not ours */
	key (canvas, GRL_KEY_RIGHT);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 206.0f);
	settle (canvas, 300);
	g_assert_cmpfloat (lrg_splitter_get_displayed_position (split), ==, 206.0f);
	key (canvas, GRL_KEY_LEFT);
	key (canvas, GRL_KEY_LEFT);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 186.0f);

	/* Home / End collapse to the minimum sizes */
	lrg_splitter_set_min_sizes (split, 40.0f, 60.0f);
	key (canvas, GRL_KEY_HOME);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 40.0f);
	key (canvas, GRL_KEY_END);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 332.0f);
	settle (canvas, 300);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 332.0f);

	/* Programmatic animation: the panes follow the spring */
	lrg_splitter_set_fraction (split, 0.25f, TRUE);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 98.0f);
	g_assert_true (lrg_canvas_update (canvas, DT));
	lrg_canvas_record (canvas);
	mid = lrg_widget_get_width (a);
	g_assert_cmpfloat (mid, <, 332.0f);
	g_assert_cmpfloat (mid, >, 98.0f);
	frames = settle (canvas, 300);
	g_assert_cmpuint (frames, >, 3);
	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 98.0f);
	g_assert_false (lrg_canvas_update (canvas, DT));

	/* Double click on the handle returns to the default fraction */
	click_at (canvas, 102.0f, 100.0f);
	click_at (canvas, 102.0f, 100.0f);
	settle (canvas, 300);
	g_assert_cmpfloat (lrg_splitter_get_position (split), ==, 196.0f);
	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 196.0f);
}

/* ==========================================================================
 * Expander
 * ========================================================================== */

static void
test_expander_animation (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgVBox             *box = lrg_vbox_new ();
	LrgExpander         *exp = lrg_expander_new ("Details");
	LrgWidget           *content = g_object_new (LRG_TYPE_WIDGET, NULL);
	LrgWidget           *below = g_object_new (LRG_TYPE_WIDGET, NULL);
	guint                toggles = 0;
	gfloat               hh;
	gfloat               full;
	gfloat               prev;
	gfloat               h;
	guint                frames = 0;

	lrg_widget_set_min_size (content, 150.0f, 100.0f);
	lrg_widget_set_min_size (below, 10.0f, 10.0f);
	lrg_expander_set_child (exp, content);
	g_object_unref (content);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (exp));
	lrg_container_add_child (LRG_CONTAINER (box), below);
	g_object_unref (exp);
	g_object_unref (below);
	g_signal_connect_swapped (exp, "toggled", G_CALLBACK (on_count_swapped), &toggles);
	place (canvas, box, 0.0f, 0.0f, 300.0f, 500.0f);
	settle (canvas, 120);

	/* Collapsed: header only, content hidden */
	hh = lrg_widget_get_height (LRG_WIDGET (exp));
	g_assert_cmpfloat (hh, >, 10.0f);
	g_assert_false (lrg_widget_get_visible (content));

	/* Expanding: the height grows frame by frame, the box below follows */
	lrg_expander_set_expanded (exp, TRUE);
	g_assert_cmpuint (toggles, ==, 1);
	g_assert_true (lrg_widget_get_visible (content));
	prev = hh;
	while (lrg_canvas_update (canvas, DT))
	{
		lrg_canvas_record (canvas);
		h = lrg_widget_get_height (LRG_WIDGET (exp));
		g_assert_cmpfloat (h, >=, hh);
		g_assert_cmpfloat (fabsf (lrg_widget_get_y (below) - (h + lrg_container_get_spacing (LRG_CONTAINER (box)))), <, 1.5f);
		if (frames < 4)
			g_assert_cmpfloat (h, >, prev);
		prev = h;
		frames++;
		g_assert_cmpuint (frames, <, 300);
	}
	lrg_canvas_record (canvas);
	g_assert_cmpuint (frames, >, 5);
	full = lrg_widget_get_height (LRG_WIDGET (exp));
	g_assert_cmpfloat (full, ==, hh + 100.0f + 16.0f);
	g_assert_false (lrg_canvas_update (canvas, DT));
	g_assert_cmpfloat (lrg_widget_get_height (content), ==, 100.0f);

	/* A click on the header collapses it again */
	click_at (canvas, 100.0f, hh * 0.5f);
	g_assert_false (lrg_expander_get_expanded (exp));
	g_assert_cmpuint (toggles, ==, 2);
	settle (canvas, 300);
	g_assert_cmpfloat (lrg_widget_get_height (LRG_WIDGET (exp)), ==, hh);
	g_assert_false (lrg_widget_get_visible (content));

	/* A click in the content area does not toggle */
	lrg_expander_set_expanded (exp, TRUE);
	settle (canvas, 300);
	click_at (canvas, 100.0f, hh + 50.0f);
	g_assert_true (lrg_expander_get_expanded (exp));

	/* ACTIVATE on the focused header toggles */
	lrg_canvas_set_focused_widget (canvas, lrg_expander_get_header (exp));
	activate (canvas);
	g_assert_false (lrg_expander_get_expanded (exp));
	g_assert_cmpuint (toggles, ==, 4);
}

/* ==========================================================================
 * Wrap box
 * ========================================================================== */

static LrgWidget *
block_new (gfloat w,
           gfloat h)
{
	LrgWidget *b = g_object_new (LRG_TYPE_WIDGET, NULL);

	lrg_widget_set_min_size (b, w, h);
	return b;
}

/* wrap_fixture: 7 blocks of 50 x 20 (block 1 is 30 high), spacing 10,
 * line spacing 8, no padding, allocated 200 wide */
static LrgWrapBox *
wrap_fixture (LrgCanvas  *canvas,
              LrgWidget **blocks)
{
	LrgWrapBox *wrap = lrg_wrap_box_new ();
	guint       i;

	lrg_container_set_spacing (LRG_CONTAINER (wrap), 10.0f);
	lrg_container_set_padding (LRG_CONTAINER (wrap), 0.0f);
	lrg_wrap_box_set_line_spacing (wrap, 8.0f);
	for (i = 0; i < 7; i++)
	{
		blocks[i] = block_new (50.0f, i == 1 ? 30.0f : 20.0f);
		lrg_container_add_child (LRG_CONTAINER (wrap), blocks[i]);
		g_object_unref (blocks[i]);
	}
	place (canvas, wrap, 0.0f, 0.0f, 200.0f, 300.0f);
	return wrap;
}

static void
test_wrap_lines (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *b[7];
	LrgWrapBox          *wrap = wrap_fixture (canvas, b);
	gfloat               pw;
	gfloat               ph;

	/* 50 + 10 + 50 + 10 + 50 = 170 <= 200: three per line */
	g_assert_cmpuint (lrg_wrap_box_get_n_lines (wrap), ==, 3);
	g_assert_cmpfloat (lrg_widget_get_x (b[2]), ==, 120.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b[3]), ==, 0.0f);
	/* Line 0 is 30 high (block 1), line 1 starts at 30 + 8 */
	g_assert_cmpfloat (lrg_widget_get_y (b[3]), ==, 38.0f);
	g_assert_cmpfloat (lrg_widget_get_y (b[6]), ==, 66.0f);

	/* Measure: natural single line wide, as tall as the current lines */
	lrg_widget_measure (LRG_WIDGET (wrap), &pw, &ph);
	g_assert_cmpfloat (pw, ==, 7.0f * 50.0f + 6.0f * 10.0f);
	g_assert_cmpfloat (ph, ==, 30.0f + 8.0f + 20.0f + 8.0f + 20.0f);

	/* Item alignment across a line */
	lrg_wrap_box_set_item_align (wrap, LRG_ALIGN_CENTER);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_y (b[0]), ==, 5.0f);
	lrg_wrap_box_set_item_align (wrap, LRG_ALIGN_END);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_y (b[0]), ==, 10.0f);
	lrg_wrap_box_set_item_align (wrap, LRG_ALIGN_FILL);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_height (b[0]), ==, 30.0f);

	/* Vertical flow wraps at the height (a plain widget measures as its
	 * current size: undo the stretch first) */
	lrg_wrap_box_set_item_align (wrap, LRG_ALIGN_START);
	lrg_widget_set_size (b[0], 50.0f, 20.0f);
	lrg_wrap_box_set_orientation (wrap, LRG_ORIENTATION_VERTICAL);
	lrg_widget_allocate (LRG_WIDGET (wrap), 0.0f, 0.0f, 300.0f, 100.0f);
	/* Column 0: 20 + 10 + 30 + 10 + 20 = 90 <= 100 */
	g_assert_cmpuint (lrg_wrap_box_get_n_lines (wrap), ==, 3);
	g_assert_cmpfloat (lrg_widget_get_y (b[2]), ==, 70.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b[3]), ==, 58.0f);
}

static void
test_wrap_justify (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *b[7];
	LrgWrapBox          *wrap = wrap_fixture (canvas, b);

	/* 30 units free on full lines; the last line has 140 free */
	lrg_wrap_box_set_justify (wrap, LRG_WRAP_JUSTIFY_CENTER);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_x (b[0]), ==, 15.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b[6]), ==, 75.0f);

	lrg_wrap_box_set_justify (wrap, LRG_WRAP_JUSTIFY_END);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_x (b[0]), ==, 30.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b[2]), ==, 150.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b[6]), ==, 150.0f);

	lrg_wrap_box_set_justify (wrap, LRG_WRAP_JUSTIFY_SPACE_BETWEEN);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_x (b[0]), ==, 0.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b[1]), ==, 75.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b[2]), ==, 150.0f);
	/* A lone item stays at the start */
	g_assert_cmpfloat (lrg_widget_get_x (b[6]), ==, 0.0f);
}

/* The parent learns the wrapped height one frame after a width change */
static void
test_wrap_remeasure (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgVBox             *box = lrg_vbox_new ();
	LrgWrapBox          *wrap = lrg_wrap_box_new ();
	guint                i;

	lrg_container_set_spacing (LRG_CONTAINER (wrap), 10.0f);
	lrg_container_set_padding (LRG_CONTAINER (wrap), 0.0f);
	lrg_container_set_padding (LRG_CONTAINER (box), 0.0f);
	for (i = 0; i < 7; i++)
	{
		LrgWidget *bl = block_new (50.0f, 20.0f);

		lrg_container_add_child (LRG_CONTAINER (wrap), bl);
		g_object_unref (bl);
	}
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (wrap));
	g_object_unref (wrap);
	place (canvas, box, 0.0f, 0.0f, 200.0f, 400.0f);

	/* First pass measured a single line; the next frame corrects it */
	lrg_canvas_update (canvas, DT);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_wrap_box_get_n_lines (wrap), ==, 3);
	g_assert_cmpfloat (lrg_widget_get_height (LRG_WIDGET (wrap)), ==, 3.0f * 20.0f + 2.0f * 8.0f);

	/* Wider: two lines */
	lrg_widget_allocate (LRG_WIDGET (box), 0.0f, 0.0f, 300.0f, 400.0f);
	lrg_canvas_update (canvas, DT);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_wrap_box_get_n_lines (wrap), ==, 2);
	g_assert_cmpfloat (lrg_widget_get_height (LRG_WIDGET (wrap)), ==, 2.0f * 20.0f + 8.0f);
}

/* ==========================================================================
 * All presets
 * ========================================================================== */

/* Every widget records in every preset; the table's stripe and the
 * splitter line take their colours from the theme */
static void
test_presets_smoke (void)
{
	const gchar * const  *presets = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	g_autoptr(LrgTheme)   theme = lrg_theme_new_preset ("midnight");
	g_autoptr(GListStore) store = store_new (100);
	LrgTable             *table = lrg_table_new ();
	LrgTileView          *tiles = lrg_tile_view_new ();
	LrgSplitter          *split = lrg_splitter_new (LRG_ORIENTATION_VERTICAL);
	LrgExpander          *exp = lrg_expander_new ("More");
	LrgWrapBox           *wrap = lrg_wrap_box_new ();
	TreeLog               log = { 0 };
	LrgTreeView          *tree;
	guint                 i;
	guint                 k;
	guint                 shapes;

	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	lrg_table_add_property_column (table, "Name", "name", 100.0f, LRG_TABLE_COLUMN_SORTABLE);
	lrg_table_set_model (table, G_LIST_MODEL (store));
	lrg_tile_view_set_model (tiles, G_LIST_MODEL (store));
	lrg_expander_set_child (exp, block_new (40.0f, 40.0f));
	g_object_unref (lrg_expander_get_child (exp));
	lrg_expander_set_expanded (exp, TRUE);
	for (i = 0; i < 5; i++)
	{
		LrgWidget *bl = block_new (30.0f, 10.0f);

		lrg_container_add_child (LRG_CONTAINER (wrap), bl);
		g_object_unref (bl);
	}
	place (canvas, table, 0.0f, 0.0f, 200.0f, 150.0f);
	place (canvas, tiles, 200.0f, 0.0f, 200.0f, 150.0f);
	place (canvas, split, 400.0f, 0.0f, 100.0f, 150.0f);
	place (canvas, exp, 0.0f, 160.0f, 200.0f, 120.0f);
	place (canvas, wrap, 200.0f, 160.0f, 100.0f, 100.0f);
	tree = tree_fixture (canvas, 50, &log);
	lrg_widget_allocate (LRG_WIDGET (tree), 500.0f, 0.0f, 200.0f, 200.0f);
	lrg_tree_view_expand (tree, lrg_tree_view_get_item (tree, 0));
	lrg_table_select (table, 1);
	lrg_tile_view_select (tiles, 0);

	for (i = 0; presets[i] != NULL; i++)
	{
		LrgPaintList *list;

		lrg_theme_apply_preset (theme, presets[i]);
		settle (canvas, 600);
		list = lrg_canvas_record (canvas);

		/* Glyphs (IMAGE instances) need the font atlas, which only
		 * exists with a window; count the shapes, which do not */
		shapes = 0;
		for (k = 0; k < lrg_paint_list_get_n_instances (list); k++)
			if (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, k)) != LRG_PAINT_PRIM_IMAGE)
				shapes++;
		g_assert_cmpuint (shapes, >, 20);
	}
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define GPU_W (256)
#define GPU_H (128)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
	const gchar *display = g_getenv ("DISPLAY");
	const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

	if ((display == NULL || display[0] == '\0') && (wayland == NULL || wayland[0] == '\0'))
		return FALSE;

	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-ui-data-widgets-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

static Image
render_canvas (LrgCanvas *canvas)
{
	RenderTexture2D target = LoadRenderTexture (GPU_W, GPU_H);
	Image           image;

	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	lrg_canvas_render (canvas);
	EndTextureMode ();

	image = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&image);
	UnloadRenderTexture (target);
	return image;
}

static gboolean
pixel_near (Image          *image,
            gint            x,
            gint            y,
            const GrlColor *c,
            gint            tol)
{
	Color p = GetImageColor (*image, x, y);

	return ABS ((gint) p.r - (gint) c->r) <= tol &&
	       ABS ((gint) p.g - (gint) c->g) <= tol &&
	       ABS ((gint) p.b - (gint) c->b) <= tol;
}

static gchar *
empty_cell (LrgTable  *table,
            LrgWidget *cell,
            GObject   *item,
            guint      position,
            gpointer   user_data)
{
	return NULL;
}

/* GPU: header in CONTROL, even rows on INPUT, odd rows SURFACE-tinted */
static void
test_gpu_table (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas)  canvas = NULL;
	g_autoptr(LrgTheme)   theme = NULL;
	g_autoptr(GListStore) store = NULL;
	LrgTable             *table;
	guint                 i;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = canvas_new ();
	theme = lrg_theme_new_preset ("midnight");
	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	table = lrg_table_new ();
	store = store_new (10);
	lrg_table_set_row_height (table, 20.0f);
	lrg_table_add_column (table, "", empty_cell, NULL, NULL, 200.0f, LRG_TABLE_COLUMN_NONE);
	lrg_table_set_model (table, G_LIST_MODEL (store));
	place (canvas, table, 0.0f, 0.0f, 240.0f, 120.0f);

	for (i = 0; presets[i] != NULL; i++)
	{
		const GrlColor *input;
		const GrlColor *surface;
		GrlColor        stripe;
		Image           img;

		lrg_theme_apply_preset (theme, presets[i]);
		input = lrg_theme_get_color (theme, LRG_THEME_COLOR_INPUT);
		surface = lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE);
		lrg_paint_color_mix (input, surface, 0.6f, &stripe);
		img = render_canvas (canvas);

		/* Header strip (1..25), row 0 (25..45), row 1 (45..65) */
		g_assert_true (pixel_near (&img, 120, 12, lrg_theme_get_color (theme, LRG_THEME_COLOR_CONTROL), 3));
		g_assert_true (pixel_near (&img, 120, 35, input, 3));
		g_assert_true (pixel_near (&img, 120, 55, &stripe, 4));
		g_assert_true (pixel_near (&img, 120, 75, input, 3));
		g_assert_true (pixel_near (&img, 250, 120, &(GrlColor){ 0, 0, 0, 255 }, 0));
		UnloadImage (img);
	}
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/ui/table/virtualized", test_table_virtualized);
	g_test_add_func ("/ui/table/sort", test_table_sort);
	g_test_add_func ("/ui/table/resize", test_table_resize);
	g_test_add_func ("/ui/table/keyboard", test_table_keyboard);
	g_test_add_func ("/ui/table/pan", test_table_pan);
	g_test_add_func ("/ui/tree/virtualized", test_tree_virtualized);
	g_test_add_func ("/ui/tree/expand", test_tree_expand);
	g_test_add_func ("/ui/tree/keyboard", test_tree_keyboard);
	g_test_add_func ("/ui/tree/multi-select", test_tree_multi_select);
	g_test_add_func ("/ui/tree/reveal", test_tree_reveal);
	g_test_add_func ("/ui/tree/reveal-parent-func", test_tree_reveal_parent_func);
	g_test_add_func ("/ui/tree/rename", test_tree_rename);
	g_test_add_func ("/ui/tree/drop-geometry", test_tree_drop_geometry);
	g_test_add_func ("/ui/tiles/virtualized", test_tiles_virtualized);
	g_test_add_func ("/ui/tiles/keyboard", test_tiles_keyboard);
	g_test_add_func ("/ui/tiles/hover-click", test_tiles_hover_click);
	g_test_add_func ("/ui/tiles/geometry-multi", test_tiles_geometry_multi);
	g_test_add_func ("/ui/list/multi-select", test_list_multi_select);
	g_test_add_func ("/ui/list/drop-geometry", test_list_drop_geometry);
	g_test_add_func ("/ui/splitter/drag", test_splitter_drag);
	g_test_add_func ("/ui/splitter/keyboard-animate", test_splitter_keyboard_animate);
	g_test_add_func ("/ui/expander/animation", test_expander_animation);
	g_test_add_func ("/ui/wrap-box/lines", test_wrap_lines);
	g_test_add_func ("/ui/wrap-box/justify", test_wrap_justify);
	g_test_add_func ("/ui/wrap-box/remeasure", test_wrap_remeasure);
	g_test_add_func ("/ui/presets/smoke", test_presets_smoke);
	g_test_add_func ("/ui/gpu/table", test_gpu_table);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
