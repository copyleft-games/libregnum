/* test-ui-new-widgets.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for overlay, scroll box, separator, spin box and list view.
 *
 * Most cases are headless: they drive input through
 * lrg_canvas_process_input() like the real loop and inspect layout
 * and the recorded paint list. Widgets step their springs from
 * lrg_canvas_update() (the canvas clock), so animation tests drive time
 * deterministically with fixed 1/60 s frames. GPU cases render
 * through a hidden window and skip without a display (run them under
 * the Xvfb wrapper).
 */

#include <glib.h>
#include <math.h>
#include <raylib.h>
#include <libregnum.h>

/* ==========================================================================
 * A solid test block: fixed preferred size, paints one opaque rect
 * ========================================================================== */

#define TEST_TYPE_BLOCK (test_block_get_type ())
G_DECLARE_FINAL_TYPE (TestBlock, test_block, TEST, BLOCK, LrgWidget)

struct _TestBlock
{
	LrgWidget parent_instance;
	gfloat    pw;
	gfloat    ph;
	GrlColor  color;
};

G_DEFINE_FINAL_TYPE (TestBlock, test_block, LRG_TYPE_WIDGET)

/* Paint calls on any TestBlock: what culling saves is the call, not the
 * instance (the paint list already drops shapes outside the clip) */
static guint block_paints = 0;

static void
test_block_measure (LrgWidget *widget,
                    gfloat    *w,
                    gfloat    *h)
{
	*w = TEST_BLOCK (widget)->pw;
	*h = TEST_BLOCK (widget)->ph;
}

static void
test_block_paint (LrgWidget    *widget,
                  LrgPaintList *list)
{
	block_paints++;
	lrg_paint_list_rect (list, lrg_widget_get_world_x (widget), lrg_widget_get_world_y (widget),
	                     lrg_widget_get_width (widget), lrg_widget_get_height (widget), 0.0f,
	                     &TEST_BLOCK (widget)->color);
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
	self->color.r = 255;
	self->color.a = 255;
}

static LrgWidget *
block_new (gfloat w,
           gfloat h)
{
	TestBlock *b = g_object_new (TEST_TYPE_BLOCK, NULL);

	b->pw = w;
	b->ph = h;
	return LRG_WIDGET (b);
}

/* ==========================================================================
 * Input helpers
 * ========================================================================== */

static void
frame_at (LrgCanvasInput *input,
          gfloat          x,
          gfloat          y)
{
	lrg_canvas_input_init (input);
	input->pointer_x = x;
	input->pointer_y = y;
	input->delta_time = 1.0f / 60.0f;
}

static void
move_to (LrgCanvas *canvas,
         gfloat     x,
         gfloat     y)
{
	LrgCanvasInput input;

	frame_at (&input, x, y);
	lrg_canvas_process_input (canvas, &input);
}

static void
press_at (LrgCanvas         *canvas,
          gfloat             x,
          gfloat             y,
          LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	frame_at (&input, x, y);
	input.modifiers = mods;
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
release_at (LrgCanvas *canvas,
            gfloat     x,
            gfloat     y)
{
	LrgCanvasInput input;

	frame_at (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	lrg_canvas_process_input (canvas, &input);
}

static void
click_mod (LrgCanvas         *canvas,
           gfloat             x,
           gfloat             y,
           LrgInputModifiers  mods)
{
	move_to (canvas, x, y);
	press_at (canvas, x, y, mods);
	release_at (canvas, x, y);
}

static void
click (LrgCanvas *canvas,
       gfloat     x,
       gfloat     y)
{
	click_mod (canvas, x, y, LRG_INPUT_MODIFIER_NONE);
}

static void
wheel_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y,
          gfloat     wheel_y)
{
	LrgCanvasInput input;

	frame_at (&input, x, y);
	input.wheel_y = wheel_y;
	lrg_canvas_process_input (canvas, &input);
}

static void
key_mod (LrgCanvas         *canvas,
         GrlKey             key,
         LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	frame_at (&input, -10.0f, -10.0f);
	input.modifiers = mods;
	lrg_canvas_input_add_key (&input, key);
	lrg_canvas_process_input (canvas, &input);
}

static void
key (LrgCanvas *canvas,
     GrlKey     k)
{
	key_mod (canvas, k, LRG_INPUT_MODIFIER_NONE);
}

/* frame: one 1/60 s update + record */
static void
frame (LrgCanvas *canvas)
{
	lrg_canvas_update (canvas, 1.0f / 60.0f);
	lrg_canvas_record (canvas);
}

/* settle_seconds: @seconds of 60 Hz canvas updates */
static void
settle_seconds (LrgCanvas *canvas,
                gfloat     seconds)
{
	gint i;

	for (i = 0; i < (gint) (seconds * 60.0f); i++)
		lrg_canvas_update (canvas, 1.0f / 60.0f);
}

/* settle: enough frames for every scroll spring to come to rest */
static void
settle (LrgCanvas *canvas)
{
	guint i;

	for (i = 0; i < 120; i++)
		frame (canvas);
}

/*
 * run_until_rest:
 *
 * Steps 60 Hz frames until lrg_canvas_update() reports that nothing
 * animates any more (fails after @max_frames).
 *
 * Returns: the number of frames that still reported motion
 */
static guint
run_until_rest (LrgCanvas *canvas,
                guint      max_frames)
{
	guint n = 0;

	while (n < max_frames && lrg_canvas_update (canvas, 1.0f / 60.0f))
		n++;
	g_assert_cmpuint (n, <, max_frames);
	return n;
}

static LrgCanvas *
canvas_new (void)
{
	LrgCanvas *canvas = lrg_canvas_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), 800.0f, 600.0f);
	lrg_canvas_set_paint_background (canvas, FALSE);
	return canvas;
}

/* ==========================================================================
 * Overlay
 * ========================================================================== */

static void
test_overlay_alignment (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	g_autoptr(LrgOverlay) overlay = lrg_overlay_new ();
	LrgWidget            *base = block_new (60.0f, 40.0f);
	LrgWidget            *badge = block_new (20.0f, 10.0f);
	gfloat                w;
	gfloat                h;

	lrg_widget_set_halign (badge, LRG_ALIGN_END);
	lrg_widget_set_valign (badge, LRG_ALIGN_START);
	lrg_widget_set_margin_all (badge, 4.0f);
	lrg_container_add_child (LRG_CONTAINER (overlay), base);
	lrg_container_add_child (LRG_CONTAINER (overlay), badge);
	g_object_unref (base);
	g_object_unref (badge);

	/* Preferred size: the biggest child with its margin */
	lrg_widget_get_preferred_size (LRG_WIDGET (overlay), &w, &h);
	g_assert_cmpfloat (w, ==, 60.0f);
	g_assert_cmpfloat (h, ==, 40.0f);

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (overlay));
	lrg_widget_allocate (LRG_WIDGET (overlay), 10.0f, 10.0f, 200.0f, 100.0f);
	lrg_canvas_record (canvas);

	/* FILL child covers it all; the badge sits top-right inside its margin */
	g_assert_cmpfloat (lrg_widget_get_width (base), ==, 200.0f);
	g_assert_cmpfloat (lrg_widget_get_height (base), ==, 100.0f);
	g_assert_cmpfloat (lrg_widget_get_x (badge), ==, 176.0f);
	g_assert_cmpfloat (lrg_widget_get_y (badge), ==, 4.0f);
	g_assert_cmpfloat (lrg_widget_get_width (badge), ==, 20.0f);

	/* Later child is on top for hit-testing */
	g_assert_true (lrg_canvas_widget_at_point (canvas, 10.0f + 180.0f, 10.0f + 8.0f) == badge);
	g_assert_true (lrg_canvas_widget_at_point (canvas, 50.0f, 50.0f) == base);
}

/* ==========================================================================
 * Scroll box
 * ========================================================================== */

/* scroll_fixture: a 100x100 box at (0,0) on a canvas, content 100x500 */
static LrgScrollBox *
scroll_fixture (LrgCanvas  *canvas,
                LrgWidget **content)
{
	LrgScrollBox *sb = lrg_scroll_box_new ();
	LrgWidget    *block = block_new (300.0f, 500.0f);

	lrg_scroll_box_set_child (sb, block);
	g_object_unref (block);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (sb));
	g_object_unref (sb);
	lrg_widget_allocate (LRG_WIDGET (sb), 0.0f, 0.0f, 100.0f, 100.0f);
	lrg_canvas_record (canvas);
	if (content != NULL)
		*content = block;
	return sb;
}

static void
test_scroll_box_clamp (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *content = NULL;
	LrgScrollBox        *sb = scroll_fixture (canvas, &content);
	gfloat               mx;
	gfloat               my;

	/* Horizontal scrolling off: content is as wide as the viewport */
	g_assert_cmpfloat (lrg_widget_get_width (content), ==, 100.0f);
	g_assert_cmpfloat (lrg_widget_get_height (content), ==, 500.0f);

	lrg_scroll_box_get_max_scroll (sb, &mx, &my);
	g_assert_cmpfloat (mx, ==, 0.0f);
	g_assert_cmpfloat (my, ==, 400.0f);

	lrg_scroll_box_scroll_to (sb, 0.0f, 1000.0f, FALSE);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 400.0f);
	g_assert_cmpfloat (lrg_widget_get_world_y (content), ==, -400.0f);

	lrg_scroll_box_scroll_to (sb, 0.0f, -50.0f, FALSE);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 0.0f);

	/* With horizontal scrolling the content keeps its width */
	lrg_scroll_box_set_hscroll (sb, TRUE);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_width (content), ==, 300.0f);
	lrg_scroll_box_get_max_scroll (sb, &mx, NULL);
	g_assert_cmpfloat (mx, ==, 200.0f);
	lrg_scroll_box_set_scroll_x (sb, 150.0f);
	g_assert_cmpfloat (lrg_widget_get_world_x (content), ==, -150.0f);
}

static void
test_scroll_box_wheel_spring (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgScrollBox        *sb = scroll_fixture (canvas, NULL);
	gfloat               first;

	/* One notch down: the target moves 48, the display glides there
	 * and the canvas reports the glide */
	wheel_at (canvas, 50.0f, 50.0f, -1.0f);
	g_assert_true (lrg_canvas_update (canvas, 1.0f / 60.0f));
	lrg_canvas_record (canvas);
	first = lrg_scroll_box_get_scroll_y (sb);
	g_assert_cmpfloat (first, >, 0.0f);
	g_assert_cmpfloat (first, <, 48.0f);

	settle (canvas);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 48.0f);

	/* Several notches accumulate, and stop at the end */
	wheel_at (canvas, 50.0f, 50.0f, -3.0f);
	settle (canvas);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 192.0f);
	wheel_at (canvas, 50.0f, 50.0f, -20.0f);
	settle (canvas);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 400.0f);
}

static void
test_scroll_box_rubber_band (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgScrollBox        *sb = scroll_fixture (canvas, NULL);
	gfloat               lowest = 0.0f;
	guint                i;

	/* Pull past the top: it overshoots, then springs back to 0 */
	wheel_at (canvas, 50.0f, 50.0f, 2.0f);
	for (i = 0; i < 120; i++)
	{
		frame (canvas);
		lowest = MIN (lowest, lrg_scroll_box_get_scroll_y (sb));
	}
	g_assert_cmpfloat (lowest, <, -5.0f);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 0.0f);

	/* Same at the bottom */
	lrg_scroll_box_scroll_to (sb, 0.0f, 400.0f, FALSE);
	wheel_at (canvas, 50.0f, 50.0f, -2.0f);
	lowest = 0.0f;
	for (i = 0; i < 120; i++)
	{
		frame (canvas);
		lowest = MAX (lowest, lrg_scroll_box_get_scroll_y (sb));
	}
	g_assert_cmpfloat (lowest, >, 405.0f);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 400.0f);

	/* Not kinetic: no overshoot at all */
	lrg_scroll_box_set_kinetic (sb, FALSE);
	wheel_at (canvas, 50.0f, 50.0f, -2.0f);
	frame (canvas);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 400.0f);
	wheel_at (canvas, 50.0f, 50.0f, 1.0f);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 352.0f);
}

static void
test_scroll_box_scroll_to_widget (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgScrollBox        *sb = lrg_scroll_box_new ();
	LrgVBox             *box = lrg_vbox_new ();
	LrgWidget           *blocks[10];
	guint                i;

	for (i = 0; i < 10; i++)
	{
		blocks[i] = block_new (80.0f, 50.0f);
		lrg_container_add_child (LRG_CONTAINER (box), blocks[i]);
		g_object_unref (blocks[i]);
	}
	lrg_scroll_box_set_child (sb, LRG_WIDGET (box));
	g_object_unref (box);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (sb));
	g_object_unref (sb);
	lrg_widget_allocate (LRG_WIDGET (sb), 0.0f, 0.0f, 100.0f, 100.0f);
	lrg_canvas_record (canvas);

	/* Block 7 spans 350..400: bottom-align it */
	lrg_scroll_box_scroll_to_widget (sb, blocks[7]);
	settle (canvas);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 300.0f);

	/* Block 2 (100..150) is above: top-align it */
	lrg_scroll_box_scroll_to_widget (sb, blocks[2]);
	settle (canvas);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 100.0f);

	/* Already visible: no movement */
	lrg_scroll_box_scroll_to_widget (sb, blocks[2]);
	settle (canvas);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 100.0f);

	/* Keyboard focus on a hidden block scrolls it into view */
	lrg_widget_set_focusable (blocks[9], TRUE);
	lrg_canvas_set_focused_widget (canvas, blocks[9]);
	lrg_widget_set_state_flags (blocks[9], LRG_WIDGET_STATE_FOCUS_VISIBLE);
	settle (canvas);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 400.0f);
}

/* find_red: index of the first red rect instance, or -1 */
static gint
find_red (LrgPaintList *list)
{
	guint i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		if (lrg_paint_instance_get_prim (inst) == LRG_PAINT_PRIM_RECT &&
		    inst->color[0] == 255 && inst->color[1] == 0 && inst->color[2] == 0 && inst->color[3] == 255)
			return (gint) i;
	}
	return -1;
}

static void
test_scroll_box_clip (void)
{
	g_autoptr(LrgCanvas)    canvas = canvas_new ();
	LrgScrollBox           *sb = scroll_fixture (canvas, NULL);
	LrgPaintList           *list;
	const LrgPaintInstance *inst;
	gint                    idx;

	lrg_widget_set_position (LRG_WIDGET (sb), 30.0f, 40.0f);
	list = lrg_canvas_record (canvas);
	idx = find_red (list);
	g_assert_cmpint (idx, >=, 0);
	inst = lrg_paint_list_get_instance (list, (guint) idx);

	/* The content is 500 tall but cut to the 100x100 viewport */
	g_assert_cmpfloat (inst->rect[3] - inst->rect[1], ==, 500.0f);
	g_assert_cmpfloat (inst->clip[0], ==, 30.0f);
	g_assert_cmpfloat (inst->clip[1], ==, 40.0f);
	g_assert_cmpfloat (inst->clip[2], ==, 130.0f);
	g_assert_cmpfloat (inst->clip[3], ==, 140.0f);
}

/*
 * Rows of a long list scrolled out of the viewport are not painted.  The
 * paint list already drops shapes outside the clip, so counting instances
 * cannot see this; what culling saves is the paint call itself (a label's
 * text shaping), so the test counts those.
 */
static void
test_scroll_box_culls (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgScrollBox        *sb = lrg_scroll_box_new ();
	LrgVBox             *rows = lrg_vbox_new ();
	LrgWidget           *last = NULL;
	guint                i;

	for (i = 0; i < 200; i++)
	{
		LrgWidget *row = block_new (100.0f, 50.0f);

		lrg_container_add_child (LRG_CONTAINER (rows), row);
		last = row;
		g_object_unref (row);
	}
	lrg_scroll_box_set_child (sb, LRG_WIDGET (rows));
	g_object_unref (rows);
	lrg_scroll_box_set_kinetic (sb, FALSE);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (sb));
	g_object_unref (sb);
	lrg_widget_allocate (LRG_WIDGET (sb), 0.0f, 0.0f, 100.0f, 100.0f);

	/* Two rows fit; the cull margin admits a few more, never two hundred */
	block_paints = 0;
	lrg_canvas_record (canvas);
	g_assert_cmpuint (block_paints, >=, 2);
	g_assert_cmpuint (block_paints, <=, 8);

	/* Scrolled to the end, the last rows are the ones painted */
	lrg_scroll_box_scroll_to (sb, 0.0f, 200.0f * 50.0f, FALSE);
	block_paints = 0;
	lrg_canvas_record (canvas);
	g_assert_cmpuint (block_paints, >=, 2);
	g_assert_cmpuint (block_paints, <=, 8);
	g_assert_cmpfloat (lrg_widget_get_world_y (last), <, 100.0f);
}

/* A box short of room takes it from its flexible children, not past its edge */
static void
test_box_shrinks_flexible_children (void)
{
	g_autoptr(LrgHBox) row = lrg_hbox_new ();
	LrgWidget         *fixed = block_new (100.0f, 20.0f);
	LrgWidget         *wide = block_new (400.0f, 20.0f);
	LrgWidget         *floor_ = block_new (400.0f, 20.0f);

	lrg_widget_set_fill_weight (wide, 1.0f);
	lrg_container_add_child (LRG_CONTAINER (row), fixed);
	lrg_container_add_child (LRG_CONTAINER (row), wide);
	g_object_unref (fixed);
	g_object_unref (wide);

	/* 500 asked for, 300 there: the flexible one gives back all 200 */
	lrg_widget_allocate (LRG_WIDGET (row), 0.0f, 0.0f, 300.0f, 20.0f);
	g_assert_cmpfloat (lrg_widget_get_width (fixed), ==, 100.0f);
	g_assert_cmpfloat (lrg_widget_get_width (wide), ==, 200.0f);
	g_assert_cmpfloat (lrg_widget_get_x (wide) + lrg_widget_get_width (wide), <=, 300.0f);

	/* never below its own minimum: what it cannot give stays a deficit */
	lrg_widget_set_min_size (wide, 250.0f, 0.0f);
	lrg_widget_allocate (LRG_WIDGET (row), 0.0f, 0.0f, 300.0f, 20.0f);
	g_assert_cmpfloat (lrg_widget_get_width (wide), ==, 250.0f);

	/* two flexible children share it by weight */
	lrg_widget_set_min_size (wide, -1.0f, -1.0f);
	lrg_widget_set_fill_weight (floor_, 3.0f);
	lrg_container_add_child (LRG_CONTAINER (row), floor_);
	g_object_unref (floor_);
	lrg_widget_allocate (LRG_WIDGET (row), 0.0f, 0.0f, 500.0f, 20.0f);
	/* 900 asked, 500 there: 400 back, 100 from the weight-1, 300 from the weight-3 */
	g_assert_cmpfloat (lrg_widget_get_width (wide), ==, 300.0f);
	g_assert_cmpfloat (lrg_widget_get_width (floor_), ==, 100.0f);

	/* with room to spare nothing changes: extra still goes by weight */
	lrg_widget_allocate (LRG_WIDGET (row), 0.0f, 0.0f, 1300.0f, 20.0f);
	g_assert_cmpfloat (lrg_widget_get_width (fixed), ==, 100.0f);
	g_assert_cmpfloat (lrg_widget_get_width (wide), ==, 500.0f);
}

/* Glyph instances recorded, for counting what a label drew */
static guint
count_glyphs (LrgPaintList *list)
{
	guint i;
	guint n = 0;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
		if (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, i)) == LRG_PAINT_PRIM_GLYPH)
			n++;
	return n;
}

/* An ellipsizing label given less than its text draws less of it */
static void
test_label_ellipsize (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgLabel            *label = lrg_label_new ("a sentence much longer than the space it is given");
	gfloat               natural = 0.0f;
	guint                whole;
	guint                cut;

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (label));
	g_object_unref (label);
	lrg_widget_measure (LRG_WIDGET (label), &natural, NULL);
	if (natural <= 0.0f)
	{
		g_test_skip ("no font to measure with");
		return;
	}

	/* still asks for the whole text */
	lrg_label_set_ellipsize (label, TRUE);
	g_assert_true (lrg_label_get_ellipsize (label));
	{
		gfloat again = 0.0f;

		lrg_widget_measure (LRG_WIDGET (label), &again, NULL);
		g_assert_cmpfloat (again, ==, natural);
	}

	/* given all of it, all of it is drawn */
	lrg_label_set_ellipsize (label, FALSE);
	lrg_widget_allocate (LRG_WIDGET (label), 0.0f, 0.0f, natural, 30.0f);
	whole = count_glyphs (lrg_canvas_record (canvas));

	/* given a third without ellipsizing, it still draws everything (past its edge) */
	lrg_widget_allocate (LRG_WIDGET (label), 0.0f, 0.0f, natural / 3.0f, 30.0f);
	g_assert_cmpuint (count_glyphs (lrg_canvas_record (canvas)), ==, whole);

	/* ellipsizing, it draws a prefix and the ellipsis: fewer glyphs */
	lrg_label_set_ellipsize (label, TRUE);
	cut = count_glyphs (lrg_canvas_record (canvas));
	g_assert_cmpuint (cut, >, 0);
	g_assert_cmpuint (cut, <, whole / 2);
}

static void
test_scroll_box_hit_test (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgWidget           *content = NULL;
	LrgScrollBox        *sb = scroll_fixture (canvas, &content);
	LrgWidget           *hit;

	/* Inside the viewport the content is hit */
	g_assert_true (lrg_canvas_widget_at_point (canvas, 40.0f, 50.0f) == content);

	/* Below the viewport the content (which extends to 500) is not */
	hit = lrg_canvas_widget_at_point (canvas, 40.0f, 300.0f);
	g_assert_true (hit != content);

	/* The scrollbar zone along the right edge belongs to the box */
	hit = lrg_canvas_widget_at_point (canvas, 96.0f, 50.0f);
	g_assert_nonnull (hit);
	g_assert_cmpstr (lrg_widget_get_name (hit), ==, "vscrollbar");
	g_assert_true (lrg_widget_get_parent (hit) == LRG_CONTAINER (sb));

	/* Clicking the track below the thumb pages down */
	lrg_scroll_box_set_kinetic (sb, FALSE);
	click (canvas, 96.0f, 95.0f);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), ==, 90.0f);

	/* Dragging the thumb follows the pointer */
	move_to (canvas, 96.0f, 30.0f);
	press_at (canvas, 96.0f, 30.0f, LRG_INPUT_MODIFIER_NONE);
	move_to (canvas, 96.0f, 60.0f);
	g_assert_cmpfloat (lrg_scroll_box_get_scroll_y (sb), >, 150.0f);
	release_at (canvas, 96.0f, 60.0f);
}

static void
test_scroll_box_scrollbar_paint (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgPaintList        *list;
	guint                before;
	guint                after;
	guint                i;
	guint                n;

	scroll_fixture (canvas, NULL);
	list = lrg_canvas_record (canvas);
	before = lrg_paint_list_get_n_instances (list);

	/* Activity fades the bar in: more shapes are recorded */
	wheel_at (canvas, 50.0f, 50.0f, -1.0f);
	for (i = 0; i < 10; i++)
		g_assert_true (lrg_canvas_update (canvas, 1.0f / 60.0f));
	list = lrg_canvas_record (canvas);
	after = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (after, >, before);

	/* The canvas stays busy through the idle wait (0.8 s of canvas
	 * time) and the fade out; then the bar is gone and it is idle */
	n = run_until_rest (canvas, 600);
	g_assert_cmpuint (n, >=, 30);
	g_assert_false (lrg_canvas_update (canvas, 1.0f / 60.0f));
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, before);
}

/* ==========================================================================
 * Separator
 * ========================================================================== */

static void
test_separator_measure (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgSeparator        *sep = lrg_separator_new ();
	LrgVBox             *box = lrg_vbox_new ();
	LrgWidget           *a = block_new (120.0f, 20.0f);
	gfloat               w;
	gfloat               h;
	gfloat               bw = lrg_theme_get_metric (lrg_theme_get_default (), LRG_THEME_METRIC_BORDER_WIDTH);

	lrg_widget_get_preferred_size (LRG_WIDGET (sep), &w, &h);
	g_assert_cmpfloat (w, ==, 0.0f);
	g_assert_cmpfloat (h, ==, bw);
	g_assert_false (lrg_widget_get_focusable (LRG_WIDGET (sep)));

	lrg_separator_set_orientation (sep, LRG_ORIENTATION_VERTICAL);
	lrg_widget_get_preferred_size (LRG_WIDGET (sep), &w, &h);
	g_assert_cmpfloat (w, ==, bw);
	g_assert_cmpfloat (h, ==, 0.0f);

	/* Horizontal in a vbox: stretched across, one line drawn */
	lrg_separator_set_orientation (sep, LRG_ORIENTATION_HORIZONTAL);
	lrg_container_add_child (LRG_CONTAINER (box), a);
	lrg_container_add_child (LRG_CONTAINER (box), LRG_WIDGET (sep));
	g_object_unref (a);
	g_object_unref (sep);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (box));
	g_object_unref (box);
	lrg_widget_allocate (LRG_WIDGET (box), 0.0f, 0.0f, 300.0f, 100.0f);
	lrg_canvas_record (canvas);
	g_assert_cmpfloat (lrg_widget_get_width (LRG_WIDGET (sep)), ==, 300.0f);
	g_assert_cmpfloat (lrg_widget_get_height (LRG_WIDGET (sep)), ==, bw);
}

/* ==========================================================================
 * Spin box
 * ========================================================================== */

static void
on_changed_count (LrgSpinBox *spin,
                  gpointer    user_data)
{
	(*(gint *) user_data)++;
}

/* spin_fixture: a 160x36 spin box at (100,100), focused */
static LrgSpinBox *
spin_fixture (LrgCanvas *canvas)
{
	LrgSpinBox *spin = lrg_spin_box_new_with_range (0.0, 50.0, 1.0);

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (spin));
	g_object_unref (spin);
	lrg_widget_allocate (LRG_WIDGET (spin), 100.0f, 100.0f, 160.0f, 36.0f);
	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (spin));
	lrg_canvas_record (canvas);
	return spin;
}

static void
test_spin_box_keyboard (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgSpinBox          *spin = spin_fixture (canvas);
	gint                 changes = 0;

	g_signal_connect (spin, "value-changed", G_CALLBACK (on_changed_count), &changes);

	key (canvas, GRL_KEY_UP);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 1.0);
	key (canvas, GRL_KEY_PAGE_UP);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 11.0);
	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 10.0);
	key (canvas, GRL_KEY_PAGE_DOWN);
	key (canvas, GRL_KEY_PAGE_DOWN);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 0.0);
	key (canvas, GRL_KEY_END);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 50.0);
	g_assert_cmpint (changes, ==, 5);   /* the second Page Down hits 0 already */

	/* Arrows were consumed: focus did not move away */
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (spin));
}

static void
test_spin_box_pointer (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgSpinBox          *spin = spin_fixture (canvas);
	gfloat               cx = 180.0f;
	gfloat               cy = 118.0f;

	lrg_spin_box_set_value (spin, 20.0);

	/* Wheel up / down steps */
	wheel_at (canvas, cx, cy, 1.0f);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 21.0);
	wheel_at (canvas, cx, cy, -1.0f);
	wheel_at (canvas, cx, cy, -1.0f);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 19.0);

	/* Buttons: minus at the left end, plus at the right end */
	click (canvas, 112.0f, cy);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 18.0);
	click (canvas, 248.0f, cy);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 19.0);

	/* Scrub: 30 units right = 5 steps; back left past the start */
	move_to (canvas, cx, cy);
	press_at (canvas, cx, cy, LRG_INPUT_MODIFIER_NONE);
	move_to (canvas, cx + 30.0f, cy);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 24.0);
	move_to (canvas, cx - 12.0f, cy);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 17.0);
	release_at (canvas, cx - 12.0f, cy);
	move_to (canvas, cx + 60.0f, cy);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 17.0);
}

/*
 * Spin box on the canvas clock: the change flash keeps the canvas busy
 * until it fades, and press-and-hold repeats after 0.4 s of canvas
 * time at 12 Hz, deterministically.
 */
static void
test_spin_box_frame_clock (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgSpinBox          *spin = spin_fixture (canvas);
	LrgCanvasInput       input;
	guint                i;

	lrg_spin_box_set_value (spin, 20.0);

	/* First frame: no flash for the value it was built with */
	run_until_rest (canvas, 240);

	/* A change flashes: busy, then idle */
	lrg_spin_box_set_value (spin, 21.0);
	g_assert_true (lrg_canvas_update (canvas, 1.0f / 60.0f));
	g_assert_cmpuint (run_until_rest (canvas, 240), >, 0);

	/* Hold plus: one step on the press, none before 0.4 s ... */
	move_to (canvas, 248.0f, 118.0f);
	press_at (canvas, 248.0f, 118.0f, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 22.0);
	for (i = 0; i < 18; i++)
	{
		frame_at (&input, 248.0f, 118.0f);
		input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
		lrg_canvas_process_input (canvas, &input);
	}
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 22.0);

	/* ... then 12 Hz: at 0.5 s held, 0.1 s into the repeat = 2 steps.
	 * Holding keeps the canvas busy. */
	for (i = 0; i < 12; i++)
	{
		frame_at (&input, 248.0f, 118.0f);
		input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
		lrg_canvas_process_input (canvas, &input);
		g_assert_true (lrg_canvas_update (canvas, 0.0f));
	}
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 24.0);

	/* Released: everything settles and the canvas goes idle */
	release_at (canvas, 248.0f, 118.0f);
	move_to (canvas, 400.0f, 300.0f);
	run_until_rest (canvas, 240);
	g_assert_false (lrg_canvas_update (canvas, 1.0f / 60.0f));
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 24.0);
}

static void
test_spin_box_clamp_digits (void)
{
	g_autoptr(LrgSpinBox) spin = lrg_spin_box_new ();
	g_autofree gchar     *text = NULL;

	lrg_spin_box_set_value (spin, 1000.0);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 100.0);
	lrg_spin_box_set_value (spin, -5.0);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 0.0);

	lrg_spin_box_set_range (spin, -1.0, 1.0);
	lrg_spin_box_set_step (spin, 0.1);
	lrg_spin_box_set_digits (spin, 2);
	lrg_spin_box_spin (spin, 3);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 0.3);
	text = lrg_spin_box_get_text (spin);
	g_assert_cmpstr (text, ==, "0.30");
	g_clear_pointer (&text, g_free);

	lrg_spin_box_set_value (spin, 3.14159);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 1.0);
	lrg_spin_box_set_max (spin, 5.0);
	lrg_spin_box_set_value (spin, 3.14159);
	text = lrg_spin_box_get_text (spin);
	g_assert_cmpstr (text, ==, "3.14");

	/* Shrinking the range clamps the value */
	lrg_spin_box_set_max (spin, 2.0);
	g_assert_cmpfloat (lrg_spin_box_get_value (spin), ==, 2.0);
}

/* ==========================================================================
 * List view
 * ========================================================================== */

/* store_new: @n items whose "name" is "item-<i>" */
static GListStore *
store_new (guint n)
{
	GListStore *store = g_list_store_new (G_TYPE_SIMPLE_ACTION);
	GPtrArray  *items = g_ptr_array_new_with_free_func (g_object_unref);
	guint       i;

	for (i = 0; i < n; i++)
	{
		g_autofree gchar *name = g_strdup_printf ("item-%u", i);

		g_ptr_array_add (items, g_simple_action_new (name, NULL));
	}
	g_list_store_splice (store, 0, 0, items->pdata, items->len);
	g_ptr_array_unref (items);
	return store;
}

/* list_fixture: a 200x300 list (32-unit rows, padding 4) at (0,0) */
static LrgListView *
list_fixture (LrgCanvas  *canvas,
              guint       n,
              GListStore **out_store)
{
	LrgListView *lv = lrg_list_view_new ();
	GListStore  *store = store_new (n);

	lrg_list_view_set_row_height (lv, 32.0f);
	lrg_list_view_set_model (lv, G_LIST_MODEL (store));
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (lv));
	g_object_unref (lv);
	lrg_widget_allocate (LRG_WIDGET (lv), 0.0f, 0.0f, 200.0f, 300.0f);
	lrg_canvas_record (canvas);
	if (out_store != NULL)
		*out_store = store;
	else
		g_object_unref (store);
	return lv;
}

/* row_y: canvas y of the middle of visible row @i (no scrolling) */
static gfloat
row_y (guint i)
{
	return 4.0f + 32.0f * (gfloat) i + 16.0f;
}

static const gchar *
row_text (LrgListView *lv,
          guint        position)
{
	LrgWidget *row = lrg_list_view_get_row_widget (lv, position);

	return row != NULL ? lrg_label_get_text (LRG_LABEL (row)) : NULL;
}

static void
test_list_view_virtualized (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgListView         *lv = list_fixture (canvas, 10000, NULL);
	guint                rows;

	/* 292 / 32 -> 10 visible rows; at most visible + 2 buffer exist */
	rows = lrg_list_view_get_n_rows (lv);
	g_assert_cmpuint (rows, <=, 12);
	g_assert_cmpuint (rows, >=, 10);
	g_assert_cmpstr (row_text (lv, 0), ==, "item-0");
	g_assert_cmpstr (row_text (lv, 9), ==, "item-9");
	g_assert_null (lrg_list_view_get_row_widget (lv, 50));

	/* Jumping far recycles the same widgets */
	lrg_list_view_scroll_to (lv, 5000, FALSE);
	lrg_canvas_record (canvas);
	g_assert_cmpstr (row_text (lv, 5000), ==, "item-5000");
	g_assert_null (lrg_list_view_get_row_widget (lv, 0));

	/* Mid-list needs one more buffer row than at the top; never more */
	rows = lrg_list_view_get_n_rows (lv);
	g_assert_cmpuint (rows, <=, 12);
	g_assert_cmpuint (lrg_container_get_child_count (LRG_CONTAINER (lv)), ==, rows + 1);

	/* Wheel scrolling rebinds rows as they enter */
	lrg_list_view_scroll_to (lv, 0, FALSE);
	wheel_at (canvas, 100.0f, 100.0f, -10.0f);
	settle (canvas);
	g_assert_cmpfloat (lrg_list_view_get_scroll_offset (lv), ==, 480.0f);
	g_assert_cmpstr (row_text (lv, 15), ==, "item-15");
	g_assert_cmpstr (row_text (lv, 24), ==, "item-24");
	g_assert_cmpuint (lrg_list_view_get_n_rows (lv), ==, rows);

	/* Rows are positioned where they belong */
	g_assert_cmpfloat (fabsf (lrg_widget_get_world_y (lrg_list_view_get_row_widget (lv, 15)) +
	                          lrg_widget_get_height (lrg_list_view_get_row_widget (lv, 15)) * 0.5f -
	                          (4.0f + 15.0f * 32.0f + 16.0f - 480.0f)), <, 1.0f);
}

typedef struct
{
	guint binds;
	guint creates;
} FactoryCounts;

static LrgWidget *
count_create (LrgListView *lv,
              gpointer     user_data)
{
	((FactoryCounts *) user_data)->creates++;
	return LRG_WIDGET (lrg_label_new (""));
}

static void
count_bind (LrgListView *lv,
            LrgWidget   *row,
            GObject     *item,
            guint        position,
            gpointer     user_data)
{
	g_autofree gchar *text = g_strdup_printf ("#%u", position);

	((FactoryCounts *) user_data)->binds++;
	lrg_label_set_text (LRG_LABEL (row), text);
}

static void
test_list_view_factory (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgListView         *lv = list_fixture (canvas, 1000, NULL);
	FactoryCounts        counts = { 0, 0 };
	guint                binds;

	lrg_list_view_set_factory (lv, count_create, count_bind, &counts, NULL);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (counts.creates, ==, lrg_list_view_get_n_rows (lv));
	g_assert_cmpstr (row_text (lv, 3), ==, "#3");

	/* A one-row scroll rebinds only the rows that changed */
	binds = counts.binds;
	lrg_list_view_scroll_to (lv, 10, FALSE);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (counts.binds - binds, ==, 1);
	g_assert_cmpstr (row_text (lv, 10), ==, "#10");
	g_assert_cmpuint (counts.creates, ==, lrg_list_view_get_n_rows (lv));
}

static void
on_selection_count (LrgListView *lv,
                    gpointer     user_data)
{
	(*(gint *) user_data)++;
}

static void
test_list_view_selection (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgListView         *lv = list_fixture (canvas, 100, NULL);
	gint                 changes = 0;

	g_signal_connect (lv, "selection-changed", G_CALLBACK (on_selection_count), &changes);

	/* Single: a click replaces the selection */
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, LRG_LIST_VIEW_INVALID_POSITION);
	click (canvas, 100.0f, row_y (2));
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, 2);
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (lv));
	click (canvas, 100.0f, row_y (4));
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, 4);
	g_assert_cmpuint (lrg_list_view_get_n_selected (lv), ==, 1);
	g_assert_cmpint (changes, ==, 2);

	/* Rows carry the SELECTED state */
	lrg_canvas_record (canvas);
	g_assert_true (lrg_widget_get_state_flags (lrg_list_view_get_row_widget (lv, 4)) &
	               LRG_WIDGET_STATE_SELECTED);
	g_assert_false (lrg_widget_get_state_flags (lrg_list_view_get_row_widget (lv, 2)) &
	                LRG_WIDGET_STATE_SELECTED);

	/* Multiple: Ctrl toggles, Shift selects a range from the anchor */
	lrg_list_view_set_selection_mode (lv, LRG_SELECTION_MODE_MULTIPLE);
	click_mod (canvas, 100.0f, row_y (6), LRG_INPUT_MODIFIER_CTRL);
	g_assert_true (lrg_list_view_is_selected (lv, 4));
	g_assert_true (lrg_list_view_is_selected (lv, 6));
	click_mod (canvas, 100.0f, row_y (4), LRG_INPUT_MODIFIER_CTRL);
	g_assert_false (lrg_list_view_is_selected (lv, 4));
	click (canvas, 100.0f, row_y (1));
	click_mod (canvas, 100.0f, row_y (5), LRG_INPUT_MODIFIER_SHIFT);
	g_assert_cmpuint (lrg_list_view_get_n_selected (lv), ==, 5);
	g_assert_true (lrg_list_view_is_selected (lv, 3));
	g_assert_false (lrg_list_view_is_selected (lv, 6));

	/* Back to single keeps one */
	lrg_list_view_set_selection_mode (lv, LRG_SELECTION_MODE_SINGLE);
	g_assert_cmpuint (lrg_list_view_get_n_selected (lv), ==, 1);

	/* None: clicks select nothing */
	lrg_list_view_set_selection_mode (lv, LRG_SELECTION_MODE_NONE);
	g_assert_cmpuint (lrg_list_view_get_n_selected (lv), ==, 0);
	click (canvas, 100.0f, row_y (7));
	g_assert_cmpuint (lrg_list_view_get_n_selected (lv), ==, 0);
	g_assert_cmpuint (lrg_list_view_get_cursor (lv), ==, 7);

	/* API */
	lrg_list_view_set_selection_mode (lv, LRG_SELECTION_MODE_MULTIPLE);
	lrg_list_view_select (lv, 10);
	lrg_list_view_select (lv, 11);
	g_assert_cmpuint (lrg_list_view_get_n_selected (lv), ==, 2);
	lrg_list_view_unselect (lv, 10);
	g_assert_false (lrg_list_view_is_selected (lv, 10));
	lrg_list_view_unselect_all (lv);
	g_assert_cmpuint (lrg_list_view_get_n_selected (lv), ==, 0);
}

static void
test_list_view_keyboard (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgListView         *lv = list_fixture (canvas, 10000, NULL);
	gfloat               max_off = 10000.0f * 32.0f - 292.0f;

	lrg_canvas_set_focused_widget (canvas, LRG_WIDGET (lv));

	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, 0);
	key (canvas, GRL_KEY_DOWN);
	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, 2);
	key (canvas, GRL_KEY_UP);
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, 1);
	g_assert_true (lrg_canvas_get_focused_widget (canvas) == LRG_WIDGET (lv));

	/* Page Down moves a page (9 whole rows) and scrolls it into view */
	key (canvas, GRL_KEY_PAGE_DOWN);
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, 10);
	settle (canvas);
	g_assert_cmpfloat (lrg_list_view_get_scroll_offset (lv), ==, 11.0f * 32.0f - 292.0f);

	/* End: last row, scrolled to the bottom and realized */
	key (canvas, GRL_KEY_END);
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, 9999);
	settle (canvas);
	g_assert_cmpfloat (lrg_list_view_get_scroll_offset (lv), ==, max_off);
	g_assert_cmpstr (row_text (lv, 9999), ==, "item-9999");

	key (canvas, GRL_KEY_DOWN);
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, 9999);

	key (canvas, GRL_KEY_HOME);
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, 0);
	settle (canvas);
	g_assert_cmpfloat (lrg_list_view_get_scroll_offset (lv), ==, 0.0f);

	/* Shift extends in multiple mode */
	lrg_list_view_set_selection_mode (lv, LRG_SELECTION_MODE_MULTIPLE);
	key_mod (canvas, GRL_KEY_DOWN, LRG_INPUT_MODIFIER_SHIFT);
	key_mod (canvas, GRL_KEY_DOWN, LRG_INPUT_MODIFIER_SHIFT);
	g_assert_cmpuint (lrg_list_view_get_n_selected (lv), ==, 3);
}

static void
on_activated (LrgListView *lv,
              guint        position,
              gpointer     user_data)
{
	*(guint *) user_data = position;
}

static void
test_list_view_activate (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgListView         *lv = list_fixture (canvas, 100, NULL);
	guint                activated = G_MAXUINT;

	g_signal_connect (lv, "row-activated", G_CALLBACK (on_activated), &activated);

	/* Single click: no activation; second click on the same row: yes */
	click (canvas, 100.0f, row_y (3));
	g_assert_cmpuint (activated, ==, G_MAXUINT);
	click (canvas, 100.0f, row_y (3));
	g_assert_cmpuint (activated, ==, 3);

	/* Clicks on different rows are not a double click */
	activated = G_MAXUINT;
	click (canvas, 100.0f, row_y (4));
	click (canvas, 100.0f, row_y (5));
	g_assert_cmpuint (activated, ==, G_MAXUINT);

	/* Enter on the cursor row */
	key (canvas, GRL_KEY_DOWN);
	key (canvas, GRL_KEY_ENTER);
	g_assert_cmpuint (activated, ==, 6);

	/* Double clicks are timed on the canvas clock: two clicks on one
	 * row half a second of frames apart are two single clicks */
	activated = G_MAXUINT;
	click (canvas, 100.0f, row_y (2));
	settle_seconds (canvas, 0.5f);
	click (canvas, 100.0f, row_y (2));
	g_assert_cmpuint (activated, ==, G_MAXUINT);
}

/*
 * The list's scroll spring and scrollbar fade run on the canvas clock:
 * lrg_canvas_update() is TRUE while the offset glides and the bar is
 * shown / fading, FALSE once all of it rests.
 */
static void
test_list_view_frame_clock (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgListView         *lv = list_fixture (canvas, 100, NULL);
	guint                n;

	(void) lv;
	run_until_rest (canvas, 600);
	g_assert_false (lrg_canvas_update (canvas, 1.0f / 60.0f));

	wheel_at (canvas, 100.0f, 100.0f, -2.0f);
	g_assert_true (lrg_canvas_update (canvas, 1.0f / 60.0f));
	n = run_until_rest (canvas, 600);
	g_assert_cmpuint (n, >=, 30);
	g_assert_false (lrg_canvas_update (canvas, 1.0f / 60.0f));
	g_assert_cmpfloat (lrg_list_view_get_scroll_offset (lv), >, 0.0f);
}

static void
test_list_view_items_changed (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	g_autoptr(GListStore) store = NULL;
	LrgListView          *lv = list_fixture (canvas, 50, &store);
	g_autoptr(GSimpleAction) a = g_simple_action_new ("new-a", NULL);
	g_autoptr(GSimpleAction) b = g_simple_action_new ("new-b", NULL);
	gint                  changes = 0;

	lrg_list_view_select (lv, 5);
	g_signal_connect (lv, "selection-changed", G_CALLBACK (on_selection_count), &changes);

	/* Insert two at the top: the selection follows its item */
	g_list_store_insert (store, 0, a);
	g_list_store_insert (store, 0, b);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_list_view_get_selected (lv), ==, 7);
	g_assert_cmpstr (row_text (lv, 0), ==, "new-b");
	g_assert_cmpstr (row_text (lv, 1), ==, "new-a");
	g_assert_cmpstr (row_text (lv, 2), ==, "item-0");

	/* Remove the selected item: selection is dropped */
	g_list_store_remove (store, 7);
	lrg_canvas_record (canvas);
	g_assert_cmpuint (lrg_list_view_get_n_selected (lv), ==, 0);
	g_assert_cmpint (changes, >=, 1);
	g_assert_cmpstr (row_text (lv, 7), ==, "item-6");

	/* Shrinking below the viewport hides spare rows and clamps scroll */
	lrg_list_view_scroll_to (lv, 40, FALSE);
	g_list_store_splice (store, 3, g_list_model_get_n_items (G_LIST_MODEL (store)) - 3, NULL, 0);
	settle (canvas);
	g_assert_cmpfloat (lrg_list_view_get_scroll_offset (lv), ==, 0.0f);
	g_assert_cmpstr (row_text (lv, 2), ==, "item-0");
	g_assert_null (lrg_list_view_get_row_widget (lv, 3));
}

static void
test_list_view_themes (void)
{
	const gchar * const *names = lrg_theme_list_presets ();
	guint                i;

	/* Every preset records without warnings and draws row highlights */
	for (i = 0; names[i] != NULL; i++)
	{
		g_autoptr(LrgCanvas) canvas = canvas_new ();
		g_autoptr(LrgTheme)  theme = lrg_theme_new_preset (names[i]);
		LrgListView         *lv;
		guint                before;

		lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
		lv = list_fixture (canvas, 20, NULL);
		before = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
		g_assert_cmpuint (before, >=, 1);

		/* Selecting a row adds its highlight */
		lrg_list_view_select (lv, 1);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (canvas)), ==, before + 1);
	}
}

/* ==========================================================================
 * GPU: scroll box clipping
 * ========================================================================== */

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
	test_window = grl_window_new (8, 8, "lrg-new-widgets-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

static void
test_gpu_scroll_box_clip (void)
{
	g_autoptr(LrgCanvas) canvas = NULL;
	LrgScrollBox        *sb;
	RenderTexture2D      target;
	Image                img;
	Color                c;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = canvas_new ();
	sb = scroll_fixture (canvas, NULL);
	lrg_widget_set_position (LRG_WIDGET (sb), 20.0f, 20.0f);
	lrg_scroll_box_scroll_to (sb, 0.0f, 100.0f, FALSE);

	target = LoadRenderTexture (160, 200);
	BeginTextureMode (target);
	ClearBackground ((Color){ 0, 0, 0, 255 });
	g_assert_true (lrg_paint_renderer_render (lrg_paint_renderer_get_default (),
	                                          lrg_canvas_record (canvas)));
	EndTextureMode ();
	img = LoadImageFromTexture (target.texture);
	ImageFlipVertical (&img);
	UnloadRenderTexture (target);

	/* Inside the viewport: the red content */
	c = GetImageColor (img, 60, 60);
	g_assert_cmpuint (c.r, >=, 250);
	g_assert_cmpuint (c.g, <=, 5);

	/* Above and below the viewport the content exists but is clipped */
	c = GetImageColor (img, 60, 10);
	g_assert_cmpuint (c.r, ==, 0);
	c = GetImageColor (img, 60, 150);
	g_assert_cmpuint (c.r, ==, 0);

	UnloadImage (img);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/ui/overlay/alignment", test_overlay_alignment);

	g_test_add_func ("/ui/scroll-box/clamp", test_scroll_box_clamp);
	g_test_add_func ("/ui/scroll-box/wheel-spring", test_scroll_box_wheel_spring);
	g_test_add_func ("/ui/scroll-box/rubber-band", test_scroll_box_rubber_band);
	g_test_add_func ("/ui/scroll-box/scroll-to-widget", test_scroll_box_scroll_to_widget);
	g_test_add_func ("/ui/scroll-box/clip", test_scroll_box_clip);
	g_test_add_func ("/ui/scroll-box/culls", test_scroll_box_culls);
	g_test_add_func ("/ui/box/shrinks-flexible-children", test_box_shrinks_flexible_children);
	g_test_add_func ("/ui/label/ellipsize", test_label_ellipsize);
	g_test_add_func ("/ui/scroll-box/hit-test", test_scroll_box_hit_test);
	g_test_add_func ("/ui/scroll-box/scrollbar-paint", test_scroll_box_scrollbar_paint);

	g_test_add_func ("/ui/separator/measure", test_separator_measure);

	g_test_add_func ("/ui/spin-box/keyboard", test_spin_box_keyboard);
	g_test_add_func ("/ui/spin-box/pointer", test_spin_box_pointer);
	g_test_add_func ("/ui/spin-box/clamp-digits", test_spin_box_clamp_digits);
	g_test_add_func ("/ui/spin-box/frame-clock", test_spin_box_frame_clock);

	g_test_add_func ("/ui/list-view/virtualized", test_list_view_virtualized);
	g_test_add_func ("/ui/list-view/factory", test_list_view_factory);
	g_test_add_func ("/ui/list-view/selection", test_list_view_selection);
	g_test_add_func ("/ui/list-view/keyboard", test_list_view_keyboard);
	g_test_add_func ("/ui/list-view/activate", test_list_view_activate);
	g_test_add_func ("/ui/list-view/frame-clock", test_list_view_frame_clock);
	g_test_add_func ("/ui/list-view/items-changed", test_list_view_items_changed);
	g_test_add_func ("/ui/list-view/themes", test_list_view_themes);

	if (!g_test_subprocess ())
		graphics_available = init_graphics_context ();
	g_test_add_func ("/ui/gpu/scroll-box-clip", test_gpu_scroll_box_clip);

	result = g_test_run ();

	g_clear_object (&test_window);
	return result;
}
