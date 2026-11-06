/* test-ui-layout.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Layout tests: slots, margins, alignment, fill weights, min/max,
 * dirty propagation, lazy layout, and the widget core (state flags,
 * springs, appear transforms, the paint/draw bridge).
 */

#include <glib.h>
#include <libregnum.h>

/* ==========================================================================
 * A fixed-size leaf for predictable measurements
 * ========================================================================== */

#define TEST_TYPE_BLOCK (test_block_get_type ())
G_DECLARE_FINAL_TYPE (TestBlock, test_block, TEST, BLOCK, LrgWidget)

struct _TestBlock
{
	LrgWidget parent_instance;
	gfloat    pref_w;
	gfloat    pref_h;
	guint     arranged;
	guint     painted;
};

G_DEFINE_FINAL_TYPE (TestBlock, test_block, LRG_TYPE_WIDGET)

static void
test_block_measure (LrgWidget *widget,
                    gfloat    *w,
                    gfloat    *h)
{
	TestBlock *self = TEST_BLOCK (widget);

	*w = self->pref_w;
	*h = self->pref_h;
}

static void
test_block_arrange (LrgWidget *widget)
{
	TEST_BLOCK (widget)->arranged++;
}

static void
test_block_paint (LrgWidget    *widget,
                  LrgPaintList *list)
{
	static const GrlColor c = { 200, 50, 50, 255 };

	TEST_BLOCK (widget)->painted++;
	lrg_paint_list_rect (list, lrg_widget_get_world_x (widget), lrg_widget_get_world_y (widget),
	                     lrg_widget_get_width (widget), lrg_widget_get_height (widget), 4.0f, &c);
}

static void
test_block_class_init (TestBlockClass *klass)
{
	LrgWidgetClass *wc = LRG_WIDGET_CLASS (klass);

	wc->measure = test_block_measure;
	wc->arrange = test_block_arrange;
	wc->paint = test_block_paint;
}

static void
test_block_init (TestBlock *self)
{
}

static LrgWidget *
block_new (gfloat w,
           gfloat h)
{
	TestBlock *b = g_object_new (TEST_TYPE_BLOCK, NULL);

	b->pref_w = w;
	b->pref_h = h;
	return LRG_WIDGET (b);
}

/* A legacy widget that only implements draw() */
#define TEST_TYPE_LEGACY (test_legacy_get_type ())
G_DECLARE_FINAL_TYPE (TestLegacy, test_legacy, TEST, LEGACY, LrgWidget)

struct _TestLegacy
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (TestLegacy, test_legacy, LRG_TYPE_WIDGET)

static void
test_legacy_draw (LrgWidget *widget)
{
}

static void
test_legacy_class_init (TestLegacyClass *klass)
{
	LRG_WIDGET_CLASS (klass)->draw = test_legacy_draw;
}

static void
test_legacy_init (TestLegacy *self)
{
}

/* ==========================================================================
 * Slots
 * ========================================================================== */

static void
test_place_in_slot_alignment (void)
{
	g_autoptr(LrgWidget) w = block_new (20.0f, 10.0f);

	/* FILL takes the whole slot (minus margin) */
	lrg_widget_set_margin_all (w, 2.0f);
	lrg_widget_place_in_slot (w, 0.0f, 0.0f, 100.0f, 50.0f);
	g_assert_cmpfloat (lrg_widget_get_x (w), ==, 2.0f);
	g_assert_cmpfloat (lrg_widget_get_width (w), ==, 96.0f);
	g_assert_cmpfloat (lrg_widget_get_height (w), ==, 46.0f);

	/* CENTER / END keep the preferred size */
	lrg_widget_set_halign (w, LRG_ALIGN_CENTER);
	lrg_widget_set_valign (w, LRG_ALIGN_END);
	lrg_widget_place_in_slot (w, 0.0f, 0.0f, 100.0f, 50.0f);
	g_assert_cmpfloat (lrg_widget_get_width (w), ==, 20.0f);
	g_assert_cmpfloat (lrg_widget_get_x (w), ==, 40.0f);   /* 2 + (96 - 20) / 2 */
	g_assert_cmpfloat (lrg_widget_get_height (w), ==, 10.0f);
	g_assert_cmpfloat (lrg_widget_get_y (w), ==, 38.0f);   /* 2 + 46 - 10 */

	lrg_widget_set_halign (w, LRG_ALIGN_START);
	lrg_widget_place_in_slot (w, 10.0f, 0.0f, 100.0f, 50.0f);
	g_assert_cmpfloat (lrg_widget_get_x (w), ==, 12.0f);
}

static void
test_min_max (void)
{
	g_autoptr(LrgWidget) w = block_new (20.0f, 10.0f);
	gfloat               pw;
	gfloat               ph;

	lrg_widget_set_min_size (w, 50.0f, 0.0f);
	lrg_widget_set_max_size (w, 0.0f, 6.0f);
	lrg_widget_get_preferred_size (w, &pw, &ph);
	g_assert_cmpfloat (pw, ==, 50.0f);
	g_assert_cmpfloat (ph, ==, 6.0f);

	/* FILL is still capped by max, and min beats a small slot */
	lrg_widget_place_in_slot (w, 0.0f, 0.0f, 30.0f, 100.0f);
	g_assert_cmpfloat (lrg_widget_get_width (w), ==, 50.0f);
	g_assert_cmpfloat (lrg_widget_get_height (w), ==, 6.0f);
}

/* ==========================================================================
 * Boxes and grid
 * ========================================================================== */

static void
test_vbox_margins_and_align (void)
{
	g_autoptr(LrgVBox) box = lrg_vbox_new ();
	LrgWidget         *a = block_new (30.0f, 10.0f);
	LrgWidget         *b = block_new (30.0f, 20.0f);

	lrg_container_set_padding (LRG_CONTAINER (box), 5.0f);
	lrg_container_set_spacing (LRG_CONTAINER (box), 4.0f);
	lrg_widget_set_margin_all (b, 3.0f);
	lrg_widget_set_halign (b, LRG_ALIGN_END);
	lrg_container_add_child (LRG_CONTAINER (box), a);
	lrg_container_add_child (LRG_CONTAINER (box), b);
	lrg_widget_allocate (LRG_WIDGET (box), 0.0f, 0.0f, 200.0f, 200.0f);

	/* a: full width (FILL), at the top */
	g_assert_cmpfloat (lrg_widget_get_x (a), ==, 5.0f);
	g_assert_cmpfloat (lrg_widget_get_y (a), ==, 5.0f);
	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 190.0f);

	/* b: after a + spacing, inset by its margin, right-aligned */
	g_assert_cmpfloat (lrg_widget_get_y (b), ==, 5.0f + 10.0f + 4.0f + 3.0f);
	g_assert_cmpfloat (lrg_widget_get_width (b), ==, 30.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b), ==, 5.0f + 190.0f - 3.0f - 30.0f);

	g_object_unref (a);
	g_object_unref (b);
}

static void
test_box_fill_weights (void)
{
	g_autoptr(LrgHBox) box = lrg_hbox_new ();
	LrgWidget         *a = block_new (10.0f, 10.0f);
	LrgWidget         *b = block_new (10.0f, 10.0f);
	LrgWidget         *c = block_new (10.0f, 10.0f);

	lrg_widget_set_fill_weight (b, 1.0f);
	lrg_widget_set_fill_weight (c, 3.0f);
	lrg_container_add_child (LRG_CONTAINER (box), a);
	lrg_container_add_child (LRG_CONTAINER (box), b);
	lrg_container_add_child (LRG_CONTAINER (box), c);
	lrg_widget_allocate (LRG_WIDGET (box), 0.0f, 0.0f, 110.0f, 20.0f);

	/* 80 spare split 1:3 -> b gets 20, c gets 60; a keeps 10 */
	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 10.0f);
	g_assert_cmpfloat (lrg_widget_get_width (b), ==, 30.0f);
	g_assert_cmpfloat (lrg_widget_get_width (c), ==, 70.0f);
	g_assert_cmpfloat (lrg_widget_get_x (c), ==, 40.0f);

	g_object_unref (a);
	g_object_unref (b);
	g_object_unref (c);
}

static void
test_box_measure_includes_margins (void)
{
	g_autoptr(LrgVBox) box = lrg_vbox_new ();
	LrgWidget         *a = block_new (30.0f, 10.0f);
	LrgWidget         *b = block_new (40.0f, 20.0f);
	gfloat             w;
	gfloat             h;

	lrg_container_set_padding (LRG_CONTAINER (box), 2.0f);
	lrg_container_set_spacing (LRG_CONTAINER (box), 6.0f);
	lrg_widget_set_margin_all (a, 1.0f);
	lrg_container_add_child (LRG_CONTAINER (box), a);
	lrg_container_add_child (LRG_CONTAINER (box), b);

	lrg_widget_measure (LRG_WIDGET (box), &w, &h);
	g_assert_cmpfloat (w, ==, 40.0f + 4.0f);
	g_assert_cmpfloat (h, ==, 12.0f + 6.0f + 20.0f + 4.0f);

	g_object_unref (a);
	g_object_unref (b);
}

static void
test_container_resize_relayouts (void)
{
	g_autoptr(LrgVBox) box = lrg_vbox_new ();
	LrgWidget         *a = block_new (10.0f, 10.0f);

	lrg_container_add_child (LRG_CONTAINER (box), a);
	lrg_widget_set_size (LRG_WIDGET (box), 100.0f, 100.0f);
	lrg_widget_layout (LRG_WIDGET (box));
	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 100.0f);

	/* Resizing the box re-runs its layout (it used not to) */
	lrg_widget_set_width (LRG_WIDGET (box), 60.0f);
	g_assert_true (lrg_widget_get_needs_layout (LRG_WIDGET (box)));
	lrg_widget_layout (LRG_WIDGET (box));
	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 60.0f);

	g_object_unref (a);
}

static void
test_grid_cells_use_slots (void)
{
	g_autoptr(LrgGrid) grid = lrg_grid_new (2);
	LrgWidget         *a = block_new (10.0f, 10.0f);
	LrgWidget         *b = block_new (10.0f, 10.0f);

	lrg_widget_set_halign (b, LRG_ALIGN_CENTER);
	lrg_container_add_child (LRG_CONTAINER (grid), a);
	lrg_container_add_child (LRG_CONTAINER (grid), b);
	lrg_widget_allocate (LRG_WIDGET (grid), 0.0f, 0.0f, 100.0f, 40.0f);

	g_assert_cmpfloat (lrg_widget_get_width (a), ==, 50.0f);
	g_assert_cmpfloat (lrg_widget_get_width (b), ==, 10.0f);
	g_assert_cmpfloat (lrg_widget_get_x (b), ==, 70.0f);

	g_object_unref (a);
	g_object_unref (b);
}

/* ==========================================================================
 * Dirty propagation
 * ========================================================================== */

static void
test_queue_layout_propagates (void)
{
	g_autoptr(LrgVBox) outer = lrg_vbox_new ();
	LrgVBox           *inner = lrg_vbox_new ();
	LrgWidget         *leaf = block_new (10.0f, 10.0f);

	lrg_container_add_child (LRG_CONTAINER (inner), leaf);
	lrg_container_add_child (LRG_CONTAINER (outer), LRG_WIDGET (inner));
	lrg_widget_allocate (LRG_WIDGET (outer), 0.0f, 0.0f, 100.0f, 100.0f);
	lrg_widget_layout (LRG_WIDGET (outer));

	g_assert_false (lrg_widget_get_needs_layout (LRG_WIDGET (outer)));
	g_assert_false (lrg_widget_get_needs_layout (LRG_WIDGET (inner)));
	g_assert_false (lrg_widget_get_needs_layout (leaf));

	lrg_widget_queue_layout (leaf);
	g_assert_true (lrg_widget_get_needs_layout (leaf));
	g_assert_true (lrg_widget_get_needs_layout (LRG_WIDGET (inner)));
	g_assert_true (lrg_widget_get_needs_layout (LRG_WIDGET (outer)));

	lrg_widget_layout (LRG_WIDGET (outer));
	g_assert_false (lrg_widget_get_needs_layout (leaf));
	g_assert_false (lrg_widget_get_needs_layout (LRG_WIDGET (outer)));

	g_object_unref (inner);
	g_object_unref (leaf);
}

static void
test_queue_layout_stops_at_marked (void)
{
	g_autoptr(LrgVBox) outer = lrg_vbox_new ();
	LrgVBox           *inner = lrg_vbox_new ();
	LrgWidget         *leaf = block_new (10.0f, 10.0f);

	lrg_container_add_child (LRG_CONTAINER (inner), leaf);
	lrg_container_add_child (LRG_CONTAINER (outer), LRG_WIDGET (inner));
	lrg_widget_allocate (LRG_WIDGET (outer), 0.0f, 0.0f, 100.0f, 100.0f);

	/* A hidden child is skipped by its parent's layout, so it stays
	 * marked while the parent is cleaned by allocate() */
	lrg_widget_set_visible (LRG_WIDGET (inner), FALSE);
	lrg_widget_queue_layout (LRG_WIDGET (inner));
	lrg_widget_allocate (LRG_WIDGET (outer), 0.0f, 0.0f, 100.0f, 100.0f);
	g_assert_true (lrg_widget_get_needs_layout (LRG_WIDGET (inner)));
	g_assert_false (lrg_widget_get_needs_layout (LRG_WIDGET (outer)));

	/* Marking the leaf stops at the already-marked inner box */
	lrg_widget_queue_layout (leaf);
	g_assert_true (lrg_widget_get_needs_layout (leaf));
	g_assert_false (lrg_widget_get_needs_layout (LRG_WIDGET (outer)));

	g_object_unref (inner);
	g_object_unref (leaf);
}

static void
test_layout_skips_clean_subtrees (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgVBox             *left = lrg_vbox_new ();
	LrgVBox             *right = lrg_vbox_new ();
	TestBlock           *a = TEST_BLOCK (block_new (10.0f, 10.0f));
	TestBlock           *b = TEST_BLOCK (block_new (10.0f, 10.0f));
	guint                a_before;
	guint                b_before;

	lrg_widget_set_size (LRG_WIDGET (canvas), 400.0f, 300.0f);
	lrg_widget_set_size (LRG_WIDGET (left), 100.0f, 100.0f);
	lrg_widget_set_size (LRG_WIDGET (right), 100.0f, 100.0f);
	lrg_container_add_child (LRG_CONTAINER (left), LRG_WIDGET (a));
	lrg_container_add_child (LRG_CONTAINER (right), LRG_WIDGET (b));
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (left));
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (right));
	lrg_canvas_record (canvas);

	a_before = a->arranged;
	b_before = b->arranged;

	/* Only the left subtree changed: only it is arranged again */
	lrg_widget_queue_layout (LRG_WIDGET (a));
	lrg_canvas_record (canvas);
	g_assert_cmpuint (a->arranged, >, a_before);
	g_assert_cmpuint (b->arranged, ==, b_before);

	/* Nothing changed: nothing is arranged */
	a_before = a->arranged;
	lrg_canvas_record (canvas);
	g_assert_cmpuint (a->arranged, ==, a_before);

	g_object_unref (left);
	g_object_unref (right);
	g_object_unref (a);
	g_object_unref (b);
}

/* ==========================================================================
 * Widget core
 * ========================================================================== */

static void
on_state_changed (LrgWidget      *w,
                  LrgWidgetState  previous,
                  gpointer        user_data)
{
	(*(gint *) user_data)++;
}

static void
test_state_flags_and_springs (void)
{
	g_autoptr(LrgWidget) w = block_new (10.0f, 10.0f);
	gint                 changes = 0;
	gint                 i;

	g_signal_connect (w, "state-changed", G_CALLBACK (on_state_changed), &changes);

	lrg_widget_set_state_flags (w, LRG_WIDGET_STATE_HOVERED);
	lrg_widget_set_state_flags (w, LRG_WIDGET_STATE_HOVERED);   /* no change */
	g_assert_cmpint (changes, ==, 1);
	g_assert_true (lrg_widget_get_state_flags (w) & LRG_WIDGET_STATE_HOVERED);

	/* The hover spring follows */
	g_assert_cmpfloat (lrg_widget_get_hover_progress (w), ==, 0.0f);
	for (i = 0; i < 120; i++)
		lrg_widget_step (w, 1.0f / 60.0f);
	g_assert_cmpfloat (lrg_widget_get_hover_progress (w), ==, 1.0f);

	lrg_widget_set_enabled (w, FALSE);
	g_assert_true (lrg_widget_get_state_flags (w) & LRG_WIDGET_STATE_DISABLED);
	lrg_widget_set_enabled (w, TRUE);
	g_assert_false (lrg_widget_get_state_flags (w) & LRG_WIDGET_STATE_DISABLED);

	lrg_widget_unset_state_flags (w, LRG_WIDGET_STATE_HOVERED);
	g_assert_cmpint (changes, ==, 4);
}

static void
test_classes_and_find (void)
{
	g_autoptr(LrgVBox) box = lrg_vbox_new ();
	LrgWidget         *w = block_new (10.0f, 10.0f);
	guint              n;

	lrg_widget_add_class (w, "primary");
	lrg_widget_add_class (w, "primary");
	lrg_widget_add_class (w, "big");
	g_assert_true (lrg_widget_has_class (w, "primary"));
	lrg_widget_get_classes (w, &n);
	g_assert_cmpuint (n, ==, 2);
	lrg_widget_remove_class (w, "primary");
	g_assert_false (lrg_widget_has_class (w, "primary"));
	g_assert_false (lrg_widget_has_class (w, "never-seen-class"));

	lrg_widget_set_name (w, "target");
	lrg_container_add_child (LRG_CONTAINER (box), w);
	g_assert_true (lrg_widget_find (LRG_WIDGET (box), "target") == w);
	g_assert_null (lrg_widget_find (LRG_WIDGET (box), "missing"));
	g_assert_true (lrg_widget_get_root (w) == LRG_WIDGET (box));
	g_assert_true (lrg_widget_is_ancestor (w, LRG_WIDGET (box)));

	g_object_unref (w);
}

static void
test_scoped_theme (void)
{
	g_autoptr(LrgVBox)  box = lrg_vbox_new ();
	g_autoptr(LrgTheme) latte = lrg_theme_new_preset ("catppuccin-latte");
	LrgWidget          *w = block_new (10.0f, 10.0f);

	lrg_container_add_child (LRG_CONTAINER (box), w);
	g_assert_true (lrg_widget_get_theme (w) == lrg_theme_get_default ());

	lrg_widget_set_theme (LRG_WIDGET (box), latte);
	g_assert_true (lrg_widget_get_theme (w) == latte);
	g_assert_cmpint (lrg_widget_get_motion (w), ==, LRG_MOTION_BOUNCY);
	lrg_widget_set_motion (w, LRG_MOTION_SNAPPY);
	g_assert_cmpint (lrg_widget_get_motion (w), ==, LRG_MOTION_SNAPPY);

	g_object_unref (w);
}

static void
test_appear_transform (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgWidget           *w = block_new (40.0f, 40.0f);
	LrgPaintList        *list;
	const LrgPaintInstance *inst;
	gint                 i;

	lrg_widget_set_size (LRG_WIDGET (canvas), 200.0f, 200.0f);
	lrg_widget_set_position (w, 50.0f, 50.0f);
	lrg_widget_set_size (w, 40.0f, 40.0f);
	lrg_widget_set_appear (w, LRG_APPEAR_FLY_UP, 0.25f);
	lrg_container_add_child (LRG_CONTAINER (canvas), w);

	/* First frame: waiting on the delay, fully transparent and low */
	list = lrg_canvas_record (canvas);
	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpuint (inst->color[3], ==, 0);
	g_assert_cmpfloat (inst->rect[1], >, 50.0f);
	g_assert_cmpfloat (lrg_widget_get_appear_progress (w), ==, 0.0f);

	/* After the delay and settling: in place, opaque */
	for (i = 0; i < 180; i++)
		lrg_canvas_update (canvas, 1.0f / 60.0f);
	list = lrg_canvas_record (canvas);
	inst = lrg_paint_list_get_instance (list, 0);
	g_assert_cmpuint (inst->color[3], ==, 255);
	g_assert_cmpfloat (inst->rect[1], ==, 50.0f);
	g_assert_cmpfloat (lrg_widget_get_appear_progress (w), ==, 1.0f);

	/* Hiding and showing replays it */
	lrg_widget_set_visible (w, FALSE);
	lrg_widget_set_visible (w, TRUE);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_instance (list, 0)->color[3], ==, 0);

	g_object_unref (w);
}

static void
test_reduced_motion_skips_appear (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgWidget           *w = block_new (40.0f, 40.0f);
	LrgPaintList        *list;

	lrg_motion_set_reduced (TRUE);
	lrg_widget_set_size (LRG_WIDGET (canvas), 200.0f, 200.0f);
	lrg_widget_set_size (w, 40.0f, 40.0f);
	lrg_widget_set_appear (w, LRG_APPEAR_POP, 0.5f);
	lrg_container_add_child (LRG_CONTAINER (canvas), w);

	lrg_canvas_record (canvas);
	lrg_canvas_update (canvas, 1.0f / 60.0f);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_instance (list, 0)->color[3], ==, 255);
	lrg_motion_set_reduced (FALSE);

	g_object_unref (w);
}

static void
test_opacity_and_invisible (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgWidget           *a = block_new (10.0f, 10.0f);
	LrgWidget           *b = block_new (10.0f, 10.0f);
	LrgPaintList        *list;

	lrg_widget_set_size (LRG_WIDGET (canvas), 100.0f, 100.0f);
	lrg_widget_set_size (a, 10.0f, 10.0f);
	lrg_widget_set_size (b, 10.0f, 10.0f);
	lrg_widget_set_opacity (a, 0.5f);
	lrg_widget_set_visible (b, FALSE);
	lrg_container_add_child (LRG_CONTAINER (canvas), a);
	lrg_container_add_child (LRG_CONTAINER (canvas), b);

	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 1);
	g_assert_cmpuint (lrg_paint_list_get_instance (list, 0)->color[3], ==, 128);

	g_object_unref (a);
	g_object_unref (b);
}

static void
test_legacy_widgets_use_callbacks (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgWidget           *legacy = g_object_new (TEST_TYPE_LEGACY, NULL);
	LrgWidget           *modern = block_new (10.0f, 10.0f);
	LrgPaintList        *list;

	lrg_widget_set_size (LRG_WIDGET (canvas), 100.0f, 100.0f);
	lrg_widget_set_size (legacy, 10.0f, 10.0f);
	lrg_widget_set_size (modern, 10.0f, 10.0f);
	lrg_container_add_child (LRG_CONTAINER (canvas), legacy);
	lrg_container_add_child (LRG_CONTAINER (canvas), modern);

	/* draw()-only widgets are recorded as callbacks between batches */
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_commands (list), ==, 2);
	g_assert_cmpint (lrg_paint_list_get_command (list, 0, NULL, NULL, NULL), ==,
	                 LRG_PAINT_COMMAND_CALLBACK);
	g_assert_cmpint (lrg_paint_list_get_command (list, 1, NULL, NULL, NULL), ==,
	                 LRG_PAINT_COMMAND_INSTANCES);

	g_object_unref (legacy);
	g_object_unref (modern);
}

static void
test_paint_box_layers (void)
{
	g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
	g_autoptr(LrgWidget)    w = block_new (10.0f, 10.0f);
	LrgPartLook             look;
	GrlColor                bg = { 10, 20, 30, 255 };
	GrlColor                sh = { 0, 0, 0, 120 };
	GrlColor                glow = { 255, 0, 255, 200 };

	lrg_part_look_init (&look);
	lrg_part_look_set_background (&look, &bg);
	lrg_part_look_set_shadow (&look, &sh, 12.0f, 4.0f);
	lrg_part_look_set_glow (&look, &glow, 8.0f);
	lrg_widget_paint_box (w, list, &look, 0.0f, 0.0f, 40.0f, 20.0f);

	/* glow, shadow, body in that order */
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 3);
	g_assert_cmpint (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, 0)), ==,
	                 LRG_PAINT_PRIM_SHADOW);
	g_assert_cmpuint (lrg_paint_list_get_instance (list, 0)->color[0], ==, 255);
	g_assert_cmpfloat (lrg_paint_list_get_instance (list, 1)->rect[1], ==, 4.0f);
	g_assert_cmpint (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, 2)), ==,
	                 LRG_PAINT_PRIM_RECT);
}

static void
test_resolve_look_blends_with_springs (void)
{
	g_autoptr(LrgTheme)     theme = lrg_theme_new ();
	g_autoptr(LrgStyleRule) rest = lrg_style_rule_new ("TestBlock", NULL, LRG_UI_PART_BOX,
	                                                   LRG_WIDGET_STATE_NONE);
	g_autoptr(LrgStyleRule) hover = lrg_style_rule_new ("TestBlock", NULL, LRG_UI_PART_BOX,
	                                                    LRG_WIDGET_STATE_HOVERED);
	g_autoptr(LrgWidget)    w = block_new (10.0f, 10.0f);
	GrlColor                black = { 0, 0, 0, 255 };
	GrlColor                white = { 255, 255, 255, 255 };
	LrgPartLook             look;
	gint                    i;

	lrg_part_look_set_background (lrg_style_rule_get_look (rest), &black);
	lrg_part_look_set_background (lrg_style_rule_get_look (hover), &white);
	lrg_theme_add_style_rule (theme, rest);
	lrg_theme_add_style_rule (theme, hover);
	lrg_theme_set_motion (theme, LRG_MOTION_SMOOTH);
	lrg_widget_set_theme (w, theme);

	lrg_widget_resolve_look (w, LRG_UI_PART_BOX, &look);
	g_assert_cmpuint (look.background.r, ==, 0);

	/* Hover: part-way there after a few frames, fully there later */
	lrg_widget_set_state_flags (w, LRG_WIDGET_STATE_HOVERED);
	for (i = 0; i < 6; i++)
		lrg_widget_step (w, 1.0f / 60.0f);
	lrg_widget_resolve_look (w, LRG_UI_PART_BOX, &look);
	g_assert_cmpuint (look.background.r, >, 0);
	g_assert_cmpuint (look.background.r, <, 255);

	for (i = 0; i < 120; i++)
		lrg_widget_step (w, 1.0f / 60.0f);
	lrg_widget_resolve_look (w, LRG_UI_PART_BOX, &look);
	g_assert_cmpuint (look.background.r, ==, 255);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui/layout/slot-alignment", test_place_in_slot_alignment);
	g_test_add_func ("/ui/layout/min-max", test_min_max);
	g_test_add_func ("/ui/layout/vbox-margins-align", test_vbox_margins_and_align);
	g_test_add_func ("/ui/layout/fill-weights", test_box_fill_weights);
	g_test_add_func ("/ui/layout/box-measure", test_box_measure_includes_margins);
	g_test_add_func ("/ui/layout/resize-relayouts", test_container_resize_relayouts);
	g_test_add_func ("/ui/layout/grid-slots", test_grid_cells_use_slots);
	g_test_add_func ("/ui/layout/dirty-propagates", test_queue_layout_propagates);
	g_test_add_func ("/ui/layout/dirty-stops", test_queue_layout_stops_at_marked);
	g_test_add_func ("/ui/layout/skips-clean", test_layout_skips_clean_subtrees);

	g_test_add_func ("/ui/widget/state-and-springs", test_state_flags_and_springs);
	g_test_add_func ("/ui/widget/classes-and-find", test_classes_and_find);
	g_test_add_func ("/ui/widget/scoped-theme", test_scoped_theme);
	g_test_add_func ("/ui/widget/appear", test_appear_transform);
	g_test_add_func ("/ui/widget/reduced-motion", test_reduced_motion_skips_appear);
	g_test_add_func ("/ui/widget/opacity-invisible", test_opacity_and_invisible);
	g_test_add_func ("/ui/widget/legacy-callbacks", test_legacy_widgets_use_callbacks);
	g_test_add_func ("/ui/widget/paint-box", test_paint_box_layers);
	g_test_add_func ("/ui/widget/look-blend", test_resolve_look_blends_with_springs);

	return g_test_run ();
}
