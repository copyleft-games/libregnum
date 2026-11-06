/* test-ui-layout-helpers.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnchorLayout, LrgSwitcher and LrgSizeBox.
 *
 * Headless: widgets sit on an 800x600 canvas, are laid out and
 * recorded with lrg_canvas_record() and stepped with lrg_canvas_update()
 * at 60 Hz, so springs are deterministic. Pages and anchored children
 * are "blocks": plain widgets with a fixed preferred size that paint
 * one rectangle whose red channel is their id, so the paint list tells
 * which block was recorded, in which order, where and how opaque.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>

#define DT (1.0f / 60.0f)

/* ==========================================================================
 * Test block: fixed preferred size, paints one tagged rectangle
 * ========================================================================== */

#define TEST_TYPE_BLOCK (test_block_get_type ())
G_DECLARE_FINAL_TYPE (TestBlock, test_block, TEST, BLOCK, LrgWidget)

struct _TestBlock
{
	LrgWidget parent_instance;
	guint8    id;
	gfloat    pref_w;
	gfloat    pref_h;
};

G_DEFINE_FINAL_TYPE (TestBlock, test_block, LRG_TYPE_WIDGET)

static void
test_block_measure (LrgWidget *widget,
                    gfloat    *w,
                    gfloat    *h)
{
	*w = TEST_BLOCK (widget)->pref_w;
	*h = TEST_BLOCK (widget)->pref_h;
}

static void
test_block_paint (LrgWidget    *widget,
                  LrgPaintList *list)
{
	GrlColor c = { TEST_BLOCK (widget)->id, 0, 0, 255 };

	lrg_paint_list_rect (list, lrg_widget_get_world_x (widget), lrg_widget_get_world_y (widget),
	                     lrg_widget_get_width (widget), lrg_widget_get_height (widget), 0.0f, &c);
}

static void
test_block_class_init (TestBlockClass *klass)
{
	LRG_WIDGET_CLASS (klass)->measure = test_block_measure;
	LRG_WIDGET_CLASS (klass)->paint = test_block_paint;
}

static void
test_block_init (TestBlock *self)
{
}

/* block_new: (transfer full) a block @id of @w x @h */
static LrgWidget *
block_new (guint8 id,
           gfloat w,
           gfloat h)
{
	TestBlock *b = g_object_new (TEST_TYPE_BLOCK, NULL);

	b->id = id;
	b->pref_w = w;
	b->pref_h = h;
	return LRG_WIDGET (b);
}

/* ==========================================================================
 * Canvas and paint-list helpers
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
run_frames (LrgCanvas *canvas,
            guint      n)
{
	guint i;

	for (i = 0; i < n; i++)
	{
		lrg_canvas_update (canvas, DT);
		lrg_canvas_record (canvas);
	}
}

/* settle: step until nothing moves (bounded) */
static void
settle (LrgCanvas *canvas)
{
	guint i;

	for (i = 0; i < 600 && lrg_canvas_update (canvas, DT); i++)
		lrg_canvas_record (canvas);
	lrg_canvas_record (canvas);
}

/* find_block: the index of the first instance painted by block @id, or -1 */
static gint
find_block (LrgPaintList *list,
            guint8        id)
{
	guint i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		if (inst->color[0] == id && inst->color[1] == 0 && inst->color[2] == 0)
			return (gint) i;
	}
	return -1;
}

static const LrgPaintInstance *
block_instance (LrgPaintList *list,
                guint8        id)
{
	gint i = find_block (list, id);

	g_assert_cmpint (i, >=, 0);
	return lrg_paint_list_get_instance (list, (guint) i);
}

static void
assert_rect (LrgWidget *w,
             gfloat     x,
             gfloat     y,
             gfloat     width,
             gfloat     height)
{
	g_assert_cmpfloat_with_epsilon (lrg_widget_get_x (w), x, 0.51f);
	g_assert_cmpfloat_with_epsilon (lrg_widget_get_y (w), y, 0.51f);
	g_assert_cmpfloat_with_epsilon (lrg_widget_get_width (w), width, 0.51f);
	g_assert_cmpfloat_with_epsilon (lrg_widget_get_height (w), height, 0.51f);
}

/* ==========================================================================
 * LrgAnchorLayout
 * ========================================================================== */

static LrgAnchorLayout *
hud_new (LrgCanvas *canvas)
{
	return place (canvas, lrg_anchor_layout_new (), 0.0f, 0.0f, 800.0f, 600.0f);
}

/* Every preset with a 100x50 child and an inward offset of (10, 20) */
static void
test_anchor_presets (void)
{
	static const struct
	{
		LrgAnchorPreset preset;
		gfloat          x;
		gfloat          y;
		gfloat          w;
		gfloat          h;
	} cases[] = {
		{ LRG_ANCHOR_PRESET_TOP_LEFT, 10, 20, 100, 50 },
		{ LRG_ANCHOR_PRESET_TOP, 400 + 10 - 50, 20, 100, 50 },
		{ LRG_ANCHOR_PRESET_TOP_RIGHT, 800 - 10 - 100, 20, 100, 50 },
		{ LRG_ANCHOR_PRESET_LEFT, 10, 300 + 20 - 25, 100, 50 },
		{ LRG_ANCHOR_PRESET_CENTER, 400 + 10 - 50, 300 + 20 - 25, 100, 50 },
		{ LRG_ANCHOR_PRESET_RIGHT, 800 - 10 - 100, 300 + 20 - 25, 100, 50 },
		{ LRG_ANCHOR_PRESET_BOTTOM_LEFT, 10, 600 - 20 - 50, 100, 50 },
		{ LRG_ANCHOR_PRESET_BOTTOM, 400 + 10 - 50, 600 - 20 - 50, 100, 50 },
		{ LRG_ANCHOR_PRESET_BOTTOM_RIGHT, 800 - 10 - 100, 600 - 20 - 50, 100, 50 },
		{ LRG_ANCHOR_PRESET_TOP_STRETCH, 10, 20, 780, 50 },
		{ LRG_ANCHOR_PRESET_MIDDLE_STRETCH, 10, 300 + 20 - 25, 780, 50 },
		{ LRG_ANCHOR_PRESET_BOTTOM_STRETCH, 10, 600 - 20 - 50, 780, 50 },
		{ LRG_ANCHOR_PRESET_LEFT_STRETCH, 10, 20, 100, 560 },
		{ LRG_ANCHOR_PRESET_CENTER_STRETCH, 400 + 10 - 50, 20, 100, 560 },
		{ LRG_ANCHOR_PRESET_RIGHT_STRETCH, 800 - 10 - 100, 20, 100, 560 },
		{ LRG_ANCHOR_PRESET_FILL, 10, 20, 780, 560 }
	};
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgAnchorLayout     *hud = hud_new (canvas);
	guint                i;

	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_ANCHOR_PRESET));
	g_assert_true (lrg_widget_get_pointer_transparent (LRG_WIDGET (hud)));

	for (i = 0; i < G_N_ELEMENTS (cases); i++)
	{
		LrgWidget *child = block_new ((guint8) (i + 1), 100.0f, 50.0f);

		lrg_anchor_layout_add (hud, child, cases[i].preset, 10.0f, 20.0f);
		g_object_unref (child);
		lrg_canvas_record (canvas);
		assert_rect (child, cases[i].x, cases[i].y, cases[i].w, cases[i].h);
	}

	/* Re-anchoring an existing child moves it */
	{
		LrgWidget *first = lrg_container_get_child (LRG_CONTAINER (hud), 0);

		lrg_anchor_layout_set_child_preset (hud, first, LRG_ANCHOR_PRESET_BOTTOM_RIGHT);
		lrg_canvas_record (canvas);
		/* Offsets stay (10, 20) as given for TOP_LEFT: now relative to
		 * the bottom-right anchor, i.e. outwards */
		assert_rect (first, 800 + 10 - 100, 600 + 20 - 50, 100, 50);
	}
}

/* Stretch between custom anchors with four margins */
static void
test_anchor_stretch (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgAnchorLayout     *hud = hud_new (canvas);
	LrgWidget           *child = block_new (1, 10.0f, 10.0f);

	lrg_container_add_child (LRG_CONTAINER (hud), child);
	g_object_unref (child);
	lrg_anchor_layout_set_child_anchors (hud, child, 0.25f, 0.0f, 0.75f, 1.0f);
	lrg_anchor_layout_set_child_offsets (hud, child, 10.0f, 20.0f, 30.0f, 40.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 210, 20, 600 - 30 - 210, 600 - 40 - 20);

	/* The layout resizes: the child follows its anchors */
	lrg_widget_allocate (LRG_WIDGET (hud), 0.0f, 0.0f, 400.0f, 300.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 110, 20, 300 - 30 - 110, 300 - 40 - 20);

	/* max below min collapses to a pinned axis */
	lrg_anchor_layout_set_child_anchors (hud, child, 0.5f, 0.5f, 0.2f, 0.2f);
	lrg_anchor_layout_set_child_offsets (hud, child, 0.0f, 0.0f, 0.0f, 0.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 200, 150, 10, 10);

	/* Padding insets the anchor area */
	lrg_container_set_padding (LRG_CONTAINER (hud), 20.0f);
	lrg_anchor_layout_set_child_anchors (hud, child, 0.0f, 0.0f, 1.0f, 1.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 20, 20, 360, 260);
}

/* Pivot / alignment, fixed sizes and the boxed data API */
static void
test_anchor_pivot (void)
{
	g_autoptr(LrgCanvas)     canvas = canvas_new ();
	LrgAnchorLayout         *hud = hud_new (canvas);
	LrgWidget               *child = block_new (1, 100.0f, 50.0f);
	LrgAnchorData            data;
	g_autoptr(LrgAnchorData) copy = NULL;
	gfloat                   x;
	gfloat                   y;
	gfloat                   w;
	gfloat                   h;

	lrg_container_add_child (LRG_CONTAINER (hud), child);
	g_object_unref (child);

	/* Defaults: top-left, auto-size */
	lrg_canvas_record (canvas);
	assert_rect (child, 0, 0, 100, 50);

	lrg_anchor_layout_set_child_anchors (hud, child, 0.5f, 0.5f, 0.5f, 0.5f);
	lrg_anchor_layout_set_child_offsets (hud, child, 10.0f, 10.0f, 0.0f, 0.0f);
	lrg_anchor_layout_set_child_alignment (hud, child, 0.0f, 0.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 410, 310, 100, 50);

	lrg_anchor_layout_set_child_alignment (hud, child, 1.0f, 1.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 310, 260, 100, 50);

	lrg_anchor_layout_set_child_alignment (hud, child, 0.5f, 0.5f);
	lrg_canvas_record (canvas);
	assert_rect (child, 360, 285, 100, 50);

	/* Fixed size through right / bottom once auto-size is off */
	lrg_anchor_layout_set_child_offsets (hud, child, 0.0f, 0.0f, 200.0f, 80.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 350, 275, 100, 50);
	lrg_anchor_layout_set_child_auto_size (hud, child, FALSE);
	lrg_canvas_record (canvas);
	assert_rect (child, 300, 260, 200, 80);

	/* Boxed data round trip */
	lrg_anchor_layout_get_child_data (hud, child, &data);
	g_assert_cmpfloat (data.min_x, ==, 0.5f);
	g_assert_cmpfloat (data.right, ==, 200.0f);
	g_assert_false (data.auto_size);
	copy = lrg_anchor_data_copy (&data);
	g_assert_cmpfloat (copy->align_x, ==, 0.5f);
	g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_ANCHOR_DATA));

	lrg_anchor_data_init (&data);
	lrg_anchor_data_set_preset (&data, LRG_ANCHOR_PRESET_BOTTOM_RIGHT);
	data.left = -5.0f;
	data.top = -5.0f;
	lrg_anchor_layout_set_child_data (hud, child, &data);
	lrg_canvas_record (canvas);
	assert_rect (child, 800 - 5 - 100, 600 - 5 - 50, 100, 50);

	/* The bare math */
	lrg_anchor_data_init (&data);
	lrg_anchor_data_set_preset (&data, LRG_ANCHOR_PRESET_CENTER);
	lrg_anchor_compute_rect (&data, 200.0f, 100.0f, 40.0f, 20.0f, &x, &y, &w, &h);
	g_assert_cmpfloat (x, ==, 80.0f);
	g_assert_cmpfloat (y, ==, 40.0f);
	g_assert_cmpfloat (w, ==, 40.0f);
	g_assert_cmpfloat (h, ==, 20.0f);
}

/* Children may lie outside; the preferred size covers pinned children */
static void
test_anchor_outside (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgAnchorLayout     *hud = hud_new (canvas);
	LrgWidget           *a = block_new (1, 100.0f, 50.0f);
	LrgWidget           *b = block_new (2, 60.0f, 40.0f);
	LrgPaintList        *list;
	gfloat               pw;
	gfloat               ph;

	lrg_anchor_layout_add (hud, a, LRG_ANCHOR_PRESET_TOP_LEFT, -50.0f, -20.0f);
	g_object_unref (a);
	lrg_container_add_child (LRG_CONTAINER (hud), b);
	g_object_unref (b);
	lrg_anchor_layout_set_child_anchors (hud, b, 1.2f, 0.5f, 1.2f, 0.5f);

	list = lrg_canvas_record (canvas);
	assert_rect (a, -50, -20, 100, 50);
	assert_rect (b, 960, 300, 60, 40);

	/* Not clipped: both are recorded at their places */
	g_assert_cmpfloat (block_instance (list, 1)->rect[0], ==, -50.0f);
	g_assert_cmpfloat (block_instance (list, 2)->rect[0], ==, 960.0f);

	/* Preferred size: a TOP_RIGHT 100x50 child with a 10 inset needs
	 * 110 x 60; a CENTER 60x40 child needs 60 x 40 */
	lrg_container_remove_all (LRG_CONTAINER (hud));
	a = block_new (3, 100.0f, 50.0f);
	lrg_anchor_layout_add (hud, a, LRG_ANCHOR_PRESET_TOP_RIGHT, 10.0f, 10.0f);
	g_object_unref (a);
	b = block_new (4, 60.0f, 40.0f);
	lrg_anchor_layout_add (hud, b, LRG_ANCHOR_PRESET_CENTER, 0.0f, 0.0f);
	g_object_unref (b);
	lrg_widget_get_preferred_size (LRG_WIDGET (hud), &pw, &ph);
	g_assert_cmpfloat (pw, ==, 110.0f);
	g_assert_cmpfloat (ph, ==, 60.0f);
}

/* Z-order sorts the child list: paint order and hit-test order agree */
static void
test_anchor_z_order (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgAnchorLayout     *hud = hud_new (canvas);
	LrgWidget           *a = block_new (1, 200.0f, 200.0f);
	LrgWidget           *b = block_new (2, 200.0f, 200.0f);
	LrgWidget           *c = block_new (3, 200.0f, 200.0f);
	LrgPaintList        *list;

	/* Three overlapping blocks, added a, b, c */
	lrg_anchor_layout_add (hud, a, LRG_ANCHOR_PRESET_TOP_LEFT, 0.0f, 0.0f);
	lrg_anchor_layout_add (hud, b, LRG_ANCHOR_PRESET_TOP_LEFT, 50.0f, 50.0f);
	lrg_anchor_layout_add (hud, c, LRG_ANCHOR_PRESET_TOP_LEFT, 100.0f, 100.0f);
	g_object_unref (a);
	g_object_unref (b);
	g_object_unref (c);

	/* Default: insertion order, c on top */
	list = lrg_canvas_record (canvas);
	g_assert_cmpint (find_block (list, 1), <, find_block (list, 2));
	g_assert_cmpint (find_block (list, 2), <, find_block (list, 3));
	g_assert_true (lrg_canvas_widget_at_point (canvas, 150.0f, 150.0f) == c);

	/* Raise a: painted last and hit first */
	lrg_anchor_layout_set_child_z_order (hud, a, 10);
	g_assert_cmpint (lrg_anchor_layout_get_child_z_order (hud, a), ==, 10);
	list = lrg_canvas_record (canvas);
	g_assert_cmpint (find_block (list, 3), <, find_block (list, 1));
	g_assert_true (lrg_container_get_child (LRG_CONTAINER (hud), 2) == a);
	g_assert_true (lrg_canvas_widget_at_point (canvas, 150.0f, 150.0f) == a);
	g_assert_true (lrg_widget_get_parent (a) == LRG_CONTAINER (hud));
	assert_rect (a, 0, 0, 200, 200);

	/* Lower c below everything; b and a keep their relative order */
	lrg_anchor_layout_set_child_z_order (hud, c, -1);
	list = lrg_canvas_record (canvas);
	g_assert_cmpint (find_block (list, 3), <, find_block (list, 2));
	g_assert_cmpint (find_block (list, 2), <, find_block (list, 1));
	g_assert_true (lrg_canvas_widget_at_point (canvas, 280.0f, 280.0f) == c);
	g_assert_true (lrg_canvas_widget_at_point (canvas, 220.0f, 220.0f) == b);

	/* A new child at z 0 goes below a (z 10) */
	{
		LrgWidget *d = block_new (4, 50.0f, 50.0f);

		lrg_anchor_layout_add (hud, d, LRG_ANCHOR_PRESET_TOP_LEFT, 0.0f, 0.0f);
		g_object_unref (d);
		g_assert_true (lrg_container_get_child (LRG_CONTAINER (hud), 3) == a);
		g_assert_true (lrg_container_get_child (LRG_CONTAINER (hud), 2) == d);
	}

	/* Empty HUD space is pointer-transparent */
	g_assert_null (lrg_canvas_widget_at_point (canvas, 700.0f, 500.0f));
}

/* Right to left mirrors anchors, offsets and pivot */
static void
test_anchor_rtl (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgAnchorLayout     *hud = hud_new (canvas);
	LrgWidget           *a = block_new (1, 100.0f, 50.0f);
	LrgWidget           *b = block_new (2, 100.0f, 50.0f);

	lrg_anchor_layout_add (hud, a, LRG_ANCHOR_PRESET_TOP_LEFT, 10.0f, 20.0f);
	lrg_anchor_layout_add (hud, b, LRG_ANCHOR_PRESET_BOTTOM_STRETCH, 10.0f, 0.0f);
	g_object_unref (a);
	g_object_unref (b);
	lrg_anchor_layout_set_child_offsets (hud, b, 10.0f, 0.0f, 30.0f, 0.0f);

	lrg_widget_set_direction (LRG_WIDGET (hud), LRG_WIDGET_DIRECTION_RTL);
	lrg_canvas_record (canvas);
	assert_rect (a, 800 - 10 - 100, 20, 100, 50);
	assert_rect (b, 30, 550, 800 - 10 - 30, 50);

	lrg_widget_set_direction (LRG_WIDGET (hud), LRG_WIDGET_DIRECTION_LTR);
	lrg_canvas_record (canvas);
	assert_rect (a, 10, 20, 100, 50);
	assert_rect (b, 10, 550, 760, 50);
}

/* ==========================================================================
 * LrgSwitcher
 * ========================================================================== */

typedef struct
{
	guint child;
	guint name;
	guint index;
	guint running;
} Notifies;

static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           gpointer    user_data)
{
	Notifies *n = user_data;

	if (g_str_equal (pspec->name, "visible-child"))
		n->child++;
	else if (g_str_equal (pspec->name, "visible-child-name"))
		n->name++;
	else if (g_str_equal (pspec->name, "visible-index"))
		n->index++;
	else if (g_str_equal (pspec->name, "transition-running"))
		n->running++;
}

/* switcher_new: a 400x300 switcher at (100, 100) with pages 1, 2, 3 */
static LrgSwitcher *
switcher_new (LrgCanvas  *canvas,
              LrgWidget **pages)
{
	LrgSwitcher        *sw = lrg_switcher_new ();
	static const gchar *names[] = { "one", "two", "three" };
	guint               i;

	for (i = 0; i < 3; i++)
	{
		pages[i] = block_new ((guint8) (i + 1), 80.0f + 40.0f * (gfloat) i, 60.0f + 20.0f * (gfloat) i);
		lrg_switcher_add_named (sw, pages[i], names[i]);
		g_object_unref (pages[i]);
	}
	return place (canvas, sw, 100.0f, 100.0f, 400.0f, 300.0f);
}

/* Only the visible page is laid out, painted and hit-tested */
static void
test_switcher_visibility (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *pages[3];
	LrgSwitcher         *sw = switcher_new (canvas, pages);
	LrgPaintList        *list;

	g_assert_true (lrg_switcher_get_visible_child (sw) == pages[0]);
	g_assert_cmpstr (lrg_switcher_get_visible_child_name (sw), ==, "one");
	g_assert_cmpint (lrg_switcher_get_visible_index (sw), ==, 0);
	g_assert_true (lrg_switcher_get_child_by_name (sw, "three") == pages[2]);
	g_assert_cmpstr (lrg_switcher_get_child_name (sw, pages[1]), ==, "two");
	g_assert_true (lrg_widget_get_visible (pages[0]));
	g_assert_false (lrg_widget_get_visible (pages[1]));
	g_assert_false (lrg_widget_get_visible (pages[2]));

	list = lrg_canvas_record (canvas);
	assert_rect (pages[0], 0, 0, 400, 300);
	g_assert_cmpint (find_block (list, 1), >=, 0);
	g_assert_cmpint (find_block (list, 2), ==, -1);
	g_assert_cmpint (find_block (list, 3), ==, -1);
	g_assert_true (lrg_canvas_widget_at_point (canvas, 300.0f, 250.0f) == pages[0]);

	/* Switching before the first frame is instant */
	lrg_switcher_set_visible_child_name (sw, "three");
	g_assert_false (lrg_switcher_get_transition_running (sw));
	list = lrg_canvas_record (canvas);
	g_assert_cmpint (find_block (list, 1), ==, -1);
	g_assert_cmpint (find_block (list, 3), >=, 0);
	g_assert_true (lrg_canvas_widget_at_point (canvas, 300.0f, 250.0f) == pages[2]);
	assert_rect (pages[2], 0, 0, 400, 300);

	/* Homogeneous: the largest page (160 x 100) */
	{
		gfloat w;
		gfloat h;

		lrg_widget_get_preferred_size (LRG_WIDGET (sw), &w, &h);
		g_assert_cmpfloat (w, ==, 160.0f);
		g_assert_cmpfloat (h, ==, 100.0f);
		lrg_switcher_set_homogeneous (sw, FALSE);
		lrg_switcher_set_visible_index (sw, 0);
		lrg_widget_get_preferred_size (LRG_WIDGET (sw), &w, &h);
		g_assert_cmpfloat (w, ==, 80.0f);
		g_assert_cmpfloat (h, ==, 60.0f);
	}

	/* Removing the visible page shows the first remaining one and
	 * gives the removed page its visibility back */
	g_object_ref (pages[0]);
	lrg_container_remove_child (LRG_CONTAINER (sw), pages[0]);
	g_assert_true (lrg_widget_get_visible (pages[0]));
	g_assert_null (lrg_switcher_get_child_name (sw, pages[0]));
	g_assert_true (lrg_switcher_get_visible_child (sw) == pages[1]);
	g_assert_true (lrg_widget_get_visible (pages[1]));
	g_object_unref (pages[0]);
}

/* Properties and their notifications (the ui.page action path) */
static void
test_switcher_properties (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *pages[3];
	LrgSwitcher         *sw = switcher_new (canvas, pages);
	Notifies             n = { 0, 0, 0, 0 };
	g_autofree gchar    *name = NULL;
	gint                 index;

	g_signal_connect (sw, "notify", G_CALLBACK (on_notify), &n);

	g_object_set (sw, "visible-child-name", "two", NULL);
	g_assert_cmpuint (n.name, ==, 1);
	g_assert_cmpuint (n.child, ==, 1);
	g_assert_cmpuint (n.index, ==, 1);
	g_object_get (sw, "visible-child-name", &name, "visible-index", &index, NULL);
	g_assert_cmpstr (name, ==, "two");
	g_assert_cmpint (index, ==, 1);

	/* Same page: nothing */
	g_object_set (sw, "visible-index", 1, NULL);
	g_assert_cmpuint (n.name, ==, 1);

	g_object_set (sw, "visible-child", pages[2], NULL);
	g_assert_cmpuint (n.child, ==, 2);
	g_assert_true (lrg_switcher_get_visible_child (sw) == pages[2]);

	/* Renaming the visible page notifies its name */
	lrg_switcher_set_child_name (sw, pages[2], "drei");
	g_assert_cmpuint (n.name, ==, 3);
	g_assert_cmpstr (lrg_switcher_get_visible_child_name (sw), ==, "drei");

	/* Unknown names warn and change nothing */
	g_test_expect_message ("Libregnum-UI", G_LOG_LEVEL_WARNING, "*no page named*");
	lrg_switcher_set_visible_child_name (sw, "nope");
	g_test_assert_expected_messages ();
	g_assert_true (lrg_switcher_get_visible_child (sw) == pages[2]);

	/* Setters notify once, and only on change */
	lrg_switcher_set_transition (sw, LRG_SWITCHER_TRANSITION_ZOOM);
	g_assert_cmpint (lrg_switcher_get_transition (sw), ==, LRG_SWITCHER_TRANSITION_ZOOM);
	lrg_switcher_set_interpolate_size (sw, TRUE);
	g_assert_true (lrg_switcher_get_interpolate_size (sw));
	g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_SWITCHER_TRANSITION));
}

/* A scroll box on a sliding page: its clip slides with its content
 * (paint-list groups move clips pushed inside them) while the
 * switcher's own clip stays put */
static void
test_switcher_slide_moves_inner_clip (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *pages[3];
	LrgSwitcher         *sw = switcher_new (canvas, pages);
	LrgScrollBox        *scroll = lrg_scroll_box_new ();
	LrgWidget           *content = block_new (77, 600.0f, 600.0f);
	LrgPaintList        *list;
	const LrgPaintInstance *inst;
	gfloat               rest_offset;
	gfloat               rest_x;

	lrg_container_add_child (LRG_CONTAINER (scroll), content);
	g_object_unref (content);
	lrg_switcher_add_named (sw, LRG_WIDGET (scroll), "scroll");
	g_object_unref (scroll);
	lrg_widget_set_motion (LRG_WIDGET (sw), LRG_MOTION_SMOOTH);
	lrg_switcher_set_transition (sw, LRG_SWITCHER_TRANSITION_SLIDE_LEFT);

	/* At rest: where the scroll box clips its content */
	lrg_switcher_set_visible_child_name (sw, "scroll");
	settle (canvas);
	list = lrg_canvas_record (canvas);
	inst = block_instance (list, 77);
	rest_x = inst->rect[0];
	rest_offset = inst->clip[2] - inst->rect[0];
	g_assert_cmpfloat (inst->clip[2], <=, 500.0f);

	/* Slide it out: the clip travels with the content */
	lrg_switcher_set_visible_child_name (sw, "one");
	run_frames (canvas, 5);
	g_assert_true (lrg_switcher_get_transition_running (sw));
	list = lrg_canvas_record (canvas);
	inst = block_instance (list, 77);
	g_assert_cmpfloat (inst->rect[0], <, rest_x - 1.0f);
	g_assert_cmpfloat_with_epsilon (inst->clip[2] - inst->rect[0], rest_offset, 0.5f);
	g_assert_cmpfloat (inst->clip[2], <, 500.0f - 1.0f);
	/* ...while the switcher's own edge still cuts the left side */
	g_assert_cmpfloat (inst->clip[0], ==, 100.0f);
}

/* A transition paints both pages, hit-tests only the new one, settles */
static void
test_switcher_transition (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *pages[3];
	LrgSwitcher         *sw = switcher_new (canvas, pages);
	Notifies             n = { 0, 0, 0, 0 };
	LrgPaintList        *list;
	const LrgPaintInstance *in;
	const LrgPaintInstance *out;
	gfloat               p;

	g_signal_connect (sw, "notify", G_CALLBACK (on_notify), &n);
	lrg_widget_set_motion (LRG_WIDGET (sw), LRG_MOTION_SMOOTH);
	lrg_switcher_set_transition (sw, LRG_SWITCHER_TRANSITION_SLIDE_LEFT);
	run_frames (canvas, 1);

	lrg_switcher_set_visible_child_name (sw, "two");
	g_assert_true (lrg_switcher_get_transition_running (sw));
	g_assert_cmpuint (n.running, ==, 1);
	g_assert_true (lrg_switcher_get_previous_child (sw) == pages[0]);
	g_assert_cmpfloat (lrg_switcher_get_transition_progress (sw), ==, 0.0f);

	run_frames (canvas, 5);
	p = lrg_switcher_get_transition_progress (sw);
	g_assert_cmpfloat (p, >, 0.0f);
	g_assert_cmpfloat (p, <, 1.0f);

	/* Both pages painted, the old one first, slid left; the new one
	 * coming in from the right; both clipped to the switcher */
	list = lrg_canvas_record (canvas);
	g_assert_cmpint (find_block (list, 1), >=, 0);
	g_assert_cmpint (find_block (list, 2), >, find_block (list, 1));
	out = block_instance (list, 1);
	in = block_instance (list, 2);
	g_assert_cmpfloat (out->rect[0], <, 100.0f);
	g_assert_cmpfloat (in->rect[0], >, 100.0f);
	g_assert_cmpfloat_with_epsilon (in->rect[0] - out->rect[0], 400.0f, 1.0f);
	g_assert_cmpfloat (in->clip[0], ==, 100.0f);
	g_assert_cmpfloat (in->clip[2], ==, 500.0f);

	/* Only the incoming page takes the pointer */
	g_assert_false (lrg_widget_get_visible (pages[0]));
	g_assert_true (lrg_canvas_widget_at_point (canvas, 150.0f, 150.0f) == pages[1]);

	settle (canvas);
	g_assert_false (lrg_switcher_get_transition_running (sw));
	g_assert_cmpuint (n.running, ==, 2);
	g_assert_null (lrg_switcher_get_previous_child (sw));
	g_assert_cmpfloat (lrg_switcher_get_transition_progress (sw), ==, 1.0f);
	list = lrg_canvas_record (canvas);
	g_assert_cmpint (find_block (list, 1), ==, -1);
	g_assert_cmpfloat (block_instance (list, 2)->rect[0], ==, 100.0f);

	/* Crossfade: the incoming alpha rises, the outgoing falls */
	lrg_switcher_set_transition (sw, LRG_SWITCHER_TRANSITION_CROSSFADE);
	lrg_switcher_set_visible_index (sw, 2);
	run_frames (canvas, 4);
	list = lrg_canvas_record (canvas);
	in = block_instance (list, 3);
	out = block_instance (list, 2);
	g_assert_cmpuint (in->color[3], >, 0);
	g_assert_cmpuint (in->color[3], <, 255);
	g_assert_cmpuint (out->color[3], <, 255);
	g_assert_cmpfloat (in->rect[0], ==, 100.0f);
	settle (canvas);

	/* Zoom: the incoming page starts smaller than its rectangle */
	lrg_switcher_set_transition (sw, LRG_SWITCHER_TRANSITION_ZOOM);
	lrg_switcher_set_visible_index (sw, 0);
	run_frames (canvas, 1);
	list = lrg_canvas_record (canvas);
	in = block_instance (list, 1);
	g_assert_cmpfloat (in->rect[2] - in->rect[0], <, 400.0f);
	g_assert_cmpfloat (in->rect[2] - in->rect[0], >, 300.0f);
	settle (canvas);
	in = block_instance (lrg_canvas_record (canvas), 1);
	g_assert_cmpfloat (in->rect[2] - in->rect[0], ==, 400.0f);
}

/* Turning a transition around is smooth; a third page restarts it */
static void
test_switcher_interrupt (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *pages[3];
	LrgSwitcher         *sw = switcher_new (canvas, pages);
	LrgPaintList        *list;
	gfloat               p;
	gfloat               old_x;
	gfloat               new_x;

	lrg_widget_set_motion (LRG_WIDGET (sw), LRG_MOTION_SMOOTH);
	lrg_switcher_set_transition (sw, LRG_SWITCHER_TRANSITION_SLIDE_LEFT);
	run_frames (canvas, 1);

	/* Half-way from one to two ... */
	lrg_switcher_set_visible_index (sw, 1);
	run_frames (canvas, 6);
	p = lrg_switcher_get_transition_progress (sw);
	list = lrg_canvas_record (canvas);
	old_x = block_instance (list, 1)->rect[0];
	new_x = block_instance (list, 2)->rect[0];

	/* ... back to one: pages keep their positions, the spring mirrors */
	lrg_switcher_set_visible_index (sw, 0);
	g_assert_true (lrg_switcher_get_transition_running (sw));
	g_assert_true (lrg_switcher_get_previous_child (sw) == pages[1]);
	g_assert_cmpfloat_with_epsilon (lrg_switcher_get_transition_progress (sw), 1.0f - p, 1e-4);
	list = lrg_canvas_record (canvas);
	g_assert_cmpfloat_with_epsilon (block_instance (list, 1)->rect[0], old_x, 0.5f);
	g_assert_cmpfloat_with_epsilon (block_instance (list, 2)->rect[0], new_x, 0.5f);
	g_assert_true (lrg_canvas_widget_at_point (canvas, 150.0f, 150.0f) == pages[0]);

	/* The motion continues back the way it came */
	run_frames (canvas, 2);
	list = lrg_canvas_record (canvas);
	g_assert_cmpfloat (block_instance (list, 1)->rect[0], >, old_x);
	settle (canvas);
	g_assert_true (lrg_switcher_get_visible_child (sw) == pages[0]);
	g_assert_cmpfloat (block_instance (lrg_canvas_record (canvas), 1)->rect[0], ==, 100.0f);

	/* A third page mid-way: the page coming in becomes the one left */
	lrg_switcher_set_visible_index (sw, 1);
	run_frames (canvas, 4);
	lrg_switcher_set_visible_index (sw, 2);
	g_assert_true (lrg_switcher_get_previous_child (sw) == pages[1]);
	g_assert_false (lrg_widget_get_visible (pages[0]));
	list = lrg_canvas_record (canvas);
	g_assert_cmpint (find_block (list, 1), ==, -1);
	settle (canvas);
	list = lrg_canvas_record (canvas);
	g_assert_cmpint (find_block (list, 2), ==, -1);
	g_assert_cmpint (find_block (list, 3), >=, 0);
	g_assert_true (lrg_widget_get_visible (pages[2]));
	g_assert_false (lrg_widget_get_visible (pages[1]));
}

/* SLIDE follows page order and mirrors right to left */
static void
test_switcher_slide_direction (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *pages[3];
	LrgSwitcher         *sw = switcher_new (canvas, pages);

	lrg_widget_set_motion (LRG_WIDGET (sw), LRG_MOTION_SMOOTH);
	lrg_switcher_set_transition (sw, LRG_SWITCHER_TRANSITION_SLIDE);
	run_frames (canvas, 1);

	/* Forward: the new page comes from the right */
	lrg_switcher_set_visible_index (sw, 2);
	run_frames (canvas, 3);
	g_assert_cmpfloat (block_instance (lrg_canvas_record (canvas), 3)->rect[0], >, 100.0f);
	settle (canvas);

	/* Backward: from the left */
	lrg_switcher_set_visible_index (sw, 1);
	run_frames (canvas, 3);
	g_assert_cmpfloat (block_instance (lrg_canvas_record (canvas), 2)->rect[0], <, 100.0f);
	settle (canvas);

	/* Right to left: forward comes from the left */
	lrg_widget_set_direction (LRG_WIDGET (sw), LRG_WIDGET_DIRECTION_RTL);
	lrg_switcher_set_visible_index (sw, 2);
	run_frames (canvas, 3);
	g_assert_cmpfloat (block_instance (lrg_canvas_record (canvas), 3)->rect[0], <, 100.0f);
	settle (canvas);

	/* Vertical slides */
	lrg_switcher_set_transition (sw, LRG_SWITCHER_TRANSITION_SLIDE_UP);
	lrg_switcher_set_visible_index (sw, 0);
	run_frames (canvas, 3);
	g_assert_cmpfloat (block_instance (lrg_canvas_record (canvas), 1)->rect[1], >, 100.0f);
	settle (canvas);
}

/* Reduced motion and LRG_MOTION_NONE switch instantly */
static void
test_switcher_reduced_motion (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *pages[3];
	LrgSwitcher         *sw = switcher_new (canvas, pages);
	LrgPaintList        *list;

	lrg_widget_set_motion (LRG_WIDGET (sw), LRG_MOTION_SMOOTH);
	run_frames (canvas, 1);

	lrg_motion_set_reduced (TRUE);
	lrg_switcher_set_visible_index (sw, 1);
	g_assert_false (lrg_switcher_get_transition_running (sw));
	list = lrg_canvas_record (canvas);
	g_assert_cmpint (find_block (list, 1), ==, -1);
	g_assert_cmpuint (block_instance (list, 2)->color[3], ==, 255);
	lrg_motion_set_reduced (FALSE);

	lrg_widget_set_motion (LRG_WIDGET (sw), LRG_MOTION_NONE);
	lrg_switcher_set_visible_index (sw, 2);
	g_assert_false (lrg_switcher_get_transition_running (sw));

	/* An explicit NONE transition is instant too */
	lrg_widget_set_motion (LRG_WIDGET (sw), LRG_MOTION_SMOOTH);
	lrg_switcher_set_visible_child_full (sw, pages[0], LRG_SWITCHER_TRANSITION_NONE);
	g_assert_false (lrg_switcher_get_transition_running (sw));
	g_assert_true (lrg_switcher_get_visible_child (sw) == pages[0]);
}

/* interpolate-size glides the preferred size between pages */
static void
test_switcher_interpolate_size (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *pages[3];
	LrgSwitcher         *sw = switcher_new (canvas, pages);
	gfloat               w;
	gfloat               h;

	lrg_widget_set_motion (LRG_WIDGET (sw), LRG_MOTION_SMOOTH);
	lrg_switcher_set_homogeneous (sw, FALSE);
	lrg_switcher_set_interpolate_size (sw, TRUE);
	lrg_widget_get_preferred_size (LRG_WIDGET (sw), &w, &h);
	g_assert_cmpfloat (w, ==, 80.0f);
	run_frames (canvas, 1);

	/* 80x60 -> 160x100 */
	lrg_switcher_set_visible_index (sw, 2);
	lrg_widget_get_preferred_size (LRG_WIDGET (sw), &w, &h);
	g_assert_cmpfloat (w, ==, 80.0f);
	run_frames (canvas, 5);
	lrg_widget_get_preferred_size (LRG_WIDGET (sw), &w, &h);
	g_assert_cmpfloat (w, >, 80.0f);
	g_assert_cmpfloat (w, <, 160.0f);
	g_assert_cmpfloat (h, >, 60.0f);
	g_assert_cmpfloat (h, <, 100.0f);
	settle (canvas);
	lrg_widget_get_preferred_size (LRG_WIDGET (sw), &w, &h);
	g_assert_cmpfloat (w, ==, 160.0f);
	g_assert_cmpfloat (h, ==, 100.0f);
}

/* ==========================================================================
 * LrgSizeBox
 * ========================================================================== */

static LrgSizeBox *
size_box_new (LrgCanvas  *canvas,
              LrgWidget **child,
              gfloat      w,
              gfloat      h)
{
	LrgSizeBox *box = lrg_size_box_new ();

	*child = block_new (1, 50.0f, 50.0f);
	lrg_size_box_set_child (box, *child);
	g_object_unref (*child);
	return place (canvas, box, 0.0f, 0.0f, w, h);
}

/* The aspect ratio fits inside both ways, placed by the child's alignment */
static void
test_size_box_aspect (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *child;
	LrgSizeBox          *box = size_box_new (canvas, &child, 800.0f, 600.0f);

	g_assert_true (lrg_size_box_get_child (box) == child);

	/* No constraint: a plain bin */
	lrg_canvas_record (canvas);
	assert_rect (child, 0, 0, 800, 600);

	/* Wider than the box: width-limited, letterboxed top and bottom */
	lrg_size_box_set_aspect_ratio (box, 2.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 0, 100, 800, 400);

	/* Taller than wide box: height-limited, pillarboxed */
	lrg_widget_allocate (LRG_WIDGET (box), 0.0f, 0.0f, 800.0f, 200.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 200, 0, 400, 200);

	/* Square in a portrait box */
	lrg_size_box_set_aspect_ratio (box, 1.0f);
	lrg_widget_allocate (LRG_WIDGET (box), 0.0f, 0.0f, 400.0f, 600.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 0, 100, 400, 400);

	/* Alignment moves it inside the bars */
	lrg_widget_set_valign (child, LRG_ALIGN_START);
	lrg_canvas_record (canvas);
	assert_rect (child, 0, 0, 400, 400);
	lrg_widget_set_valign (child, LRG_ALIGN_END);
	lrg_canvas_record (canvas);
	assert_rect (child, 0, 200, 400, 400);

	lrg_widget_allocate (LRG_WIDGET (box), 0.0f, 0.0f, 600.0f, 400.0f);
	lrg_widget_set_halign (child, LRG_ALIGN_END);
	lrg_canvas_record (canvas);
	assert_rect (child, 200, 0, 400, 400);

	/* Right to left: END is on the left */
	lrg_widget_set_direction (LRG_WIDGET (box), LRG_WIDGET_DIRECTION_RTL);
	lrg_canvas_record (canvas);
	assert_rect (child, 0, 0, 400, 400);
	lrg_widget_set_direction (LRG_WIDGET (box), LRG_WIDGET_DIRECTION_INHERIT);

	/* Unset */
	lrg_size_box_set_aspect_ratio (box, -3.0f);
	g_assert_cmpfloat (lrg_size_box_get_aspect_ratio (box), ==, 0.0f);
}

/* Overrides and limits, in layout and in the preferred size */
static void
test_size_box_limits (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *child;
	LrgSizeBox          *box = size_box_new (canvas, &child, 800.0f, 600.0f);
	gfloat               w;
	gfloat               h;
	gfloat               v;

	/* Max: a FILL child is centred in the narrower slot */
	lrg_size_box_set_max_width (box, 300.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 250, 0, 300, 600);
	lrg_widget_get_preferred_size (LRG_WIDGET (box), &w, &h);
	g_assert_cmpfloat (w, ==, 50.0f);

	/* Min grows the preferred size */
	lrg_size_box_set_min_width (box, 120.0f);
	lrg_size_box_set_min_height (box, 90.0f);
	lrg_widget_get_preferred_size (LRG_WIDGET (box), &w, &h);
	g_assert_cmpfloat (w, ==, 120.0f);
	g_assert_cmpfloat (h, ==, 90.0f);

	/* Max height caps the slot; a CENTER child centres in it */
	lrg_size_box_set_max_height (box, 200.0f);
	lrg_widget_set_halign (child, LRG_ALIGN_CENTER);
	lrg_widget_set_valign (child, LRG_ALIGN_CENTER);
	lrg_canvas_record (canvas);
	assert_rect (child, 375, 275, 50, 50);

	/* Overrides: exact size, and the ratio derives the other axis */
	lrg_size_box_set_min_width (box, -1.0f);
	lrg_size_box_set_min_height (box, -1.0f);
	lrg_size_box_set_max_width (box, -5.0f);
	lrg_size_box_set_max_height (box, -1.0f);
	g_assert_cmpfloat (lrg_size_box_get_max_width (box), ==, -1.0f);
	lrg_size_box_set_width_override (box, 200.0f);
	lrg_size_box_set_aspect_ratio (box, 2.0f);
	lrg_widget_get_preferred_size (LRG_WIDGET (box), &w, &h);
	g_assert_cmpfloat (w, ==, 200.0f);
	g_assert_cmpfloat (h, ==, 100.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 300, 250, 200, 100);

	lrg_size_box_set_width_override (box, -1.0f);
	lrg_size_box_set_height_override (box, 30.0f);
	g_assert_cmpfloat (lrg_size_box_get_height_override (box), ==, 30.0f);
	lrg_widget_get_preferred_size (LRG_WIDGET (box), &w, &h);
	g_assert_cmpfloat (w, ==, 60.0f);
	g_assert_cmpfloat (h, ==, 30.0f);

	/* Properties */
	g_object_set (box, "min-height", 10.0f, NULL);
	g_object_get (box, "min-height", &v, NULL);
	g_assert_cmpfloat (v, ==, 10.0f);
	g_assert_cmpfloat (lrg_size_box_get_min_height (box), ==, 10.0f);
	g_assert_cmpfloat (lrg_size_box_get_width_override (box), ==, -1.0f);
	g_assert_cmpfloat (lrg_size_box_get_min_width (box), ==, -1.0f);
	g_assert_cmpfloat (lrg_size_box_get_max_height (box), ==, -1.0f);

	/* Replacing the child */
	lrg_size_box_set_child (box, NULL);
	g_assert_null (lrg_size_box_get_child (box));
}

/* A size box with a ratio inside an hbox */
static void
test_size_box_in_hbox (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgHBox             *hbox = place (canvas, lrg_hbox_new (), 0.0f, 0.0f, 600.0f, 200.0f);
	LrgSizeBox          *box = lrg_size_box_new ();
	LrgWidget           *child = block_new (1, 50.0f, 50.0f);
	LrgWidget           *after = block_new (2, 40.0f, 40.0f);

	lrg_size_box_set_child (box, child);
	g_object_unref (child);
	lrg_size_box_set_width_override (box, 120.0f);
	lrg_size_box_set_aspect_ratio (box, 1.5f);
	lrg_container_add_child (LRG_CONTAINER (hbox), LRG_WIDGET (box));
	g_object_unref (box);
	lrg_container_add_child (LRG_CONTAINER (hbox), after);
	g_object_unref (after);
	lrg_canvas_record (canvas);

	/* The box asks for 120 x 80; the hbox gives it 120 wide and the
	 * full height; the child keeps the ratio, centred vertically */
	assert_rect (LRG_WIDGET (box), 0, 0, 120, 200);
	assert_rect (child, 0, 60, 120, 80);
	g_assert_cmpfloat (lrg_widget_get_x (after), ==, 120.0f);

	/* Growing the box with a fill weight keeps the child at 120 x 80,
	 * centred in the wider slot */
	lrg_widget_set_fill_weight (LRG_WIDGET (box), 1.0f);
	lrg_canvas_record (canvas);
	assert_rect (LRG_WIDGET (box), 0, 0, 560, 200);
	assert_rect (child, 220, 60, 120, 80);

	/* Without the override the ratio fits the slot: 300 x 200 */
	lrg_size_box_set_width_override (box, -1.0f);
	lrg_canvas_record (canvas);
	assert_rect (child, 130, 0, 300, 200);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui/anchor-layout/presets", test_anchor_presets);
	g_test_add_func ("/ui/anchor-layout/stretch", test_anchor_stretch);
	g_test_add_func ("/ui/anchor-layout/pivot", test_anchor_pivot);
	g_test_add_func ("/ui/anchor-layout/outside", test_anchor_outside);
	g_test_add_func ("/ui/anchor-layout/z-order", test_anchor_z_order);
	g_test_add_func ("/ui/anchor-layout/rtl", test_anchor_rtl);
	g_test_add_func ("/ui/switcher/visibility", test_switcher_visibility);
	g_test_add_func ("/ui/switcher/properties", test_switcher_properties);
	g_test_add_func ("/ui/switcher/transition", test_switcher_transition);
	g_test_add_func ("/ui/switcher/slide-moves-inner-clip", test_switcher_slide_moves_inner_clip);
	g_test_add_func ("/ui/switcher/interrupt", test_switcher_interrupt);
	g_test_add_func ("/ui/switcher/slide-direction", test_switcher_slide_direction);
	g_test_add_func ("/ui/switcher/reduced-motion", test_switcher_reduced_motion);
	g_test_add_func ("/ui/switcher/interpolate-size", test_switcher_interpolate_size);
	g_test_add_func ("/ui/size-box/aspect", test_size_box_aspect);
	g_test_add_func ("/ui/size-box/limits", test_size_box_limits);
	g_test_add_func ("/ui/size-box/in-hbox", test_size_box_in_hbox);

	return g_test_run ();
}
