/* test-ui-block-editor.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBlockEditor: label templates, building and walking programs
 * through the API, snapping driven by pointer drags on a canvas,
 * palette drags (create / delete), undo and redo of every edit, YAML
 * round trips, view maths, slot editing through TEXT_INPUT, dropdown
 * menus, keyboard commands, headless paint lists and one GPU pixel
 * check (skipped without a display; run it under xrun.sh).
 *
 * Every animation is driven with lrg_canvas_update() at 60 Hz, so the
 * tests are deterministic.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

/* ==========================================================================
 * Canvas and input helpers
 * ========================================================================== */

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
press_button (LrgCanvas         *canvas,
              GrlMouseButton     button,
              gfloat             x,
              gfloat             y,
              LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.modifiers = mods;
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (button);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (button);
	lrg_canvas_process_input (canvas, &input);
}

static void
hold_button (LrgCanvas      *canvas,
             GrlMouseButton  button,
             gfloat          x,
             gfloat          y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (button);
	lrg_canvas_process_input (canvas, &input);
}

static void
release_button (LrgCanvas      *canvas,
                GrlMouseButton  button,
                gfloat          x,
                gfloat          y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (button);
	lrg_canvas_process_input (canvas, &input);
}

/* drag_with: a full drag of @button from (x0, y0) to (x1, y1) in 8 moves */
static void
drag_with (LrgCanvas      *canvas,
           GrlMouseButton  button,
           gfloat          x0,
           gfloat          y0,
           gfloat          x1,
           gfloat          y1)
{
	guint i;

	move_to (canvas, x0, y0);
	press_button (canvas, button, x0, y0, LRG_INPUT_MODIFIER_NONE);
	for (i = 1; i <= 8; i++)
	{
		gfloat t = (gfloat) i / 8.0f;

		hold_button (canvas, button, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	release_button (canvas, button, x1, y1);
}

static void
drag (LrgCanvas *canvas,
      gfloat     x0,
      gfloat     y0,
      gfloat     x1,
      gfloat     y1)
{
	drag_with (canvas, GRL_MOUSE_BUTTON_LEFT, x0, y0, x1, y1);
}

static void
click_at (LrgCanvas *canvas,
          gfloat     x,
          gfloat     y)
{
	move_to (canvas, x, y);
	press_button (canvas, GRL_MOUSE_BUTTON_LEFT, x, y, LRG_INPUT_MODIFIER_NONE);
	release_button (canvas, GRL_MOUSE_BUTTON_LEFT, x, y);
}

static void
key (LrgCanvas         *canvas,
     GrlKey             k,
     LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	frame_pointer (&input, 999.0f, 699.0f);
	input.modifiers = mods;
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

static void
type_text (LrgCanvas   *canvas,
           const gchar *text)
{
	LrgCanvasInput input;

	frame_pointer (&input, 999.0f, 699.0f);
	lrg_canvas_input_add_text (&input, text);
	lrg_canvas_process_input (canvas, &input);
}

static void
wheel (LrgCanvas         *canvas,
       gfloat             x,
       gfloat             y,
       gfloat             amount,
       LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.wheel_y = amount;
	input.modifiers = mods;
	lrg_canvas_process_input (canvas, &input);
}

/* settle: run frames until nothing animates */
static void
settle (LrgCanvas *canvas)
{
	guint n = 0;

	while (n < 600 && lrg_canvas_update (canvas, DT))
	{
		lrg_canvas_record (canvas);
		n++;
	}
	lrg_canvas_record (canvas);
	g_assert_cmpuint (n, <, 600);
}

static void
count_cb (gpointer user_data)
{
	(*(guint *) user_data)++;
}

static void
last_block_cb (LrgBlockEditor *editor,
               LrgBlock       *block,
               gpointer        user_data)
{
	*(LrgBlock **) user_data = block;
}

/* ==========================================================================
 * Fixture: a 1000 x 700 canvas filled by an editor with a palette of
 * every shape. The workspace starts right of the palette (64 + 220).
 * ========================================================================== */

typedef struct
{
	LrgCanvas      *canvas;
	LrgBlockEditor *editor;
} Fixture;

static void
define_palette (LrgBlockEditor *editor)
{
	g_autoptr(GError) error = NULL;

	g_assert_nonnull (lrg_block_editor_define_kind (editor, "when-flag", LRG_BLOCK_SHAPE_HAT, "events",
	                                                "when flag clicked", &error));
	g_assert_nonnull (lrg_block_editor_define_kind (editor, "move", LRG_BLOCK_SHAPE_STATEMENT, "motion",
	                                                "move {steps:number=10} steps", &error));
	g_assert_nonnull (lrg_block_editor_define_kind (editor, "x-pos", LRG_BLOCK_SHAPE_REPORTER, "motion",
	                                                "x position", &error));
	g_assert_nonnull (lrg_block_editor_define_kind (editor, "say", LRG_BLOCK_SHAPE_STATEMENT, "looks",
	                                                "say {msg:text=Hello}", &error));
	g_assert_nonnull (lrg_block_editor_define_kind (editor, "repeat", LRG_BLOCK_SHAPE_C, "control",
	                                                "repeat {times:number=10}", &error));
	g_assert_nonnull (lrg_block_editor_define_kind (editor, "if-else", LRG_BLOCK_SHAPE_E, "control",
	                                                "if {cond:boolean} then|else", &error));
	g_assert_nonnull (lrg_block_editor_define_kind (editor, "stop", LRG_BLOCK_SHAPE_CAP, "control",
	                                                "stop {what:dropdown=all|this script}", &error));
	g_assert_nonnull (lrg_block_editor_define_kind (editor, "touching", LRG_BLOCK_SHAPE_BOOLEAN, "sensing",
	                                                "touching {what:dropdown=edge|mouse} ?", &error));
	g_assert_nonnull (lrg_block_editor_define_kind (editor, "add", LRG_BLOCK_SHAPE_REPORTER, "operators",
	                                                "{a:number} + {b:number}", &error));
	g_assert_no_error (error);
}

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	f->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (f->canvas), 1000.0f, 700.0f);
	lrg_canvas_set_paint_background (f->canvas, FALSE);
	f->editor = lrg_block_editor_new ();
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (f->editor));
	g_object_unref (f->editor);
	lrg_widget_allocate (LRG_WIDGET (f->editor), 0.0f, 0.0f, 1000.0f, 700.0f);
	define_palette (f->editor);
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->editor));
	settle (f->canvas);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->canvas);
}

static gchar *
program (LrgBlockEditor *editor)
{
	g_autoptr(GError) error = NULL;
	gchar            *yaml = lrg_block_editor_save_yaml (editor, LRG_BLOCK_YAML_PROGRAM, NULL, &error);

	g_assert_no_error (error);
	g_assert_nonnull (yaml);
	return yaml;
}

/* center: the screen centre of a block's top-left corner area */
static void
corner (LrgBlockEditor *editor,
        LrgBlock       *b,
        gfloat         *x,
        gfloat         *y)
{
	g_assert_true (lrg_block_editor_get_block_bounds (editor, b, x, y, NULL, NULL));
}

/* ==========================================================================
 * Kinds and label templates
 * ========================================================================== */

static void
test_kind_parse (void)
{
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgBlockKind) kind = NULL;
	const gchar * const    *choices;

	/* --- slots with types and defaults --- */
	kind = lrg_block_kind_new ("glide", LRG_BLOCK_SHAPE_STATEMENT, "motion",
	                           "glide {secs:number=1.5} secs to {target:dropdown=mouse|random} {note:text}", &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_block_kind_get_n_slots (kind), ==, 3);
	g_assert_cmpuint (lrg_block_kind_get_n_mouths (kind), ==, 0);
	g_assert_cmpstr (lrg_block_kind_get_slot_name (kind, 0), ==, "secs");
	g_assert_cmpint (lrg_block_kind_get_slot_type (kind, 0), ==, LRG_BLOCK_SLOT_NUMBER);
	g_assert_cmpstr (lrg_block_kind_get_slot_default (kind, 0), ==, "1.5");
	g_assert_cmpint (lrg_block_kind_get_slot_type (kind, 1), ==, LRG_BLOCK_SLOT_DROPDOWN);
	g_assert_cmpstr (lrg_block_kind_get_slot_default (kind, 1), ==, "mouse");
	choices = lrg_block_kind_get_slot_choices (kind, 1);
	g_assert_cmpuint (g_strv_length ((gchar **) choices), ==, 2);
	g_assert_cmpstr (choices[1], ==, "random");
	g_assert_null (lrg_block_kind_get_slot_choices (kind, 0));
	g_assert_cmpint (lrg_block_kind_get_slot_type (kind, 2), ==, LRG_BLOCK_SLOT_TEXT);
	g_assert_cmpstr (lrg_block_kind_get_slot_default (kind, 2), ==, "");
	g_assert_cmpint (lrg_block_kind_find_slot (kind, "target"), ==, 1);
	g_assert_cmpint (lrg_block_kind_find_slot (kind, "nope"), ==, -1);
	g_assert_cmpstr (lrg_block_kind_get_label (kind), ==,
	                 "glide {secs:number=1.5} secs to {target:dropdown=mouse|random} {note:text}");
	g_clear_object (&kind);

	/* --- number default, E rows, escapes --- */
	kind = lrg_block_kind_new ("ife", LRG_BLOCK_SHAPE_E, NULL, "if {c:boolean} then|else", &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_block_kind_get_n_mouths (kind), ==, 2);
	g_assert_cmpint (lrg_block_kind_get_slot_type (kind, 0), ==, LRG_BLOCK_SLOT_BOOLEAN);
	g_clear_object (&kind);
	kind = lrg_block_kind_new ("lit", LRG_BLOCK_SHAPE_STATEMENT, NULL, "set \\{x\\} \\| {v:number}", &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_block_kind_get_n_slots (kind), ==, 1);
	g_assert_cmpstr (lrg_block_kind_get_slot_default (kind, 0), ==, "0");
	g_clear_object (&kind);

	/* --- malformed templates --- */
	{
		static const gchar * const bad[] = {
			"move {steps:number steps",       /* unterminated */
			"move {steps:float}",             /* unknown type */
			"{a:number} {a:text}",            /* duplicate name */
			"say {:text}",                    /* empty name */
			"say {msg}",                      /* no type */
			"pick {k:dropdown}",              /* dropdown without choices */
			"a | b",                          /* '|' outside an E block */
			"oops }",                         /* unbalanced */
			"end \\"                          /* dangling escape */
		};
		guint i;

		for (i = 0; i < G_N_ELEMENTS (bad); i++)
		{
			kind = lrg_block_kind_new ("bad", LRG_BLOCK_SHAPE_STATEMENT, NULL, bad[i], &error);
			g_assert_null (kind);
			g_assert_error (error, LRG_BLOCK_EDITOR_ERROR, LRG_BLOCK_EDITOR_ERROR_LABEL);
			g_clear_error (&error);
		}
	}

	/* --- custom dropdown choices --- */
	kind = lrg_block_kind_new ("go", LRG_BLOCK_SHAPE_STATEMENT, NULL, "go to {s:dropdown=a}", &error);
	{
		const gchar *sprites[] = { "cat", "dog", NULL };

		lrg_block_kind_set_slot_choices (kind, 0, sprites);
		g_assert_cmpstr (lrg_block_kind_get_slot_default (kind, 0), ==, "cat");
		g_assert_cmpstr (lrg_block_kind_get_slot_choices (kind, 0)[1], ==, "dog");
	}
}

/* ==========================================================================
 * Building and walking programs through the API
 * ========================================================================== */

static void
test_build_api (Fixture       *f,
                gconstpointer  data)
{
	LrgBlockEditor   *e = f->editor;
	LrgBlock         *hat;
	LrgBlock         *move;
	LrgBlock         *rep;
	LrgBlock         *say;
	LrgBlock         *ife;
	LrgBlock         *xpos;
	LrgBlock         *touch;
	LrgBlock         *stop;
	LrgBlock         *say2;
	guint             added = 0;
	guint             removed = 0;
	guint             changed = 0;
	guint             index = 99;
	g_autoptr(GVariant) v = NULL;

	g_signal_connect_swapped (e, "block-added", G_CALLBACK (count_cb), &added);
	g_signal_connect_swapped (e, "block-removed", G_CALLBACK (count_cb), &removed);
	g_signal_connect_swapped (e, "changed", G_CALLBACK (count_cb), &changed);

	hat = lrg_block_editor_create_block (e, "when-flag", 40.0f, 40.0f);
	move = lrg_block_editor_create_block (e, "move", 300.0f, 40.0f);
	rep = lrg_block_editor_create_block (e, "repeat", 300.0f, 200.0f);
	say = lrg_block_editor_create_block (e, "say", 300.0f, 400.0f);
	g_assert_null (lrg_block_editor_create_block (e, "unknown", 0.0f, 0.0f));
	g_assert_cmpuint (added, ==, 4);
	g_assert_cmpuint (changed, ==, 4);
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 4);
	g_assert_true (lrg_block_get_editor (hat) == e);

	/* --- a script: hat, move, repeat { say } --- */
	g_assert_true (lrg_block_editor_attach (e, move, hat, LRG_BLOCK_PLACE_NEXT, 0));
	g_assert_true (lrg_block_editor_attach (e, rep, move, LRG_BLOCK_PLACE_NEXT, 0));
	g_assert_true (lrg_block_editor_attach (e, say, rep, LRG_BLOCK_PLACE_MOUTH, 0));
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 1);
	g_assert_true (lrg_block_get_next (hat) == move);
	g_assert_true (lrg_block_get_next (move) == rep);
	g_assert_true (lrg_block_get_mouth (rep, 0) == say);
	g_assert_true (lrg_block_get_parent (say) == rep);
	g_assert_cmpint (lrg_block_get_place (say, &index), ==, LRG_BLOCK_PLACE_MOUTH);
	g_assert_cmpuint (index, ==, 0);
	g_assert_true (lrg_block_get_top (say) == hat);
	g_assert_cmpint (lrg_block_get_place (hat, NULL), ==, LRG_BLOCK_PLACE_NONE);

	/* --- values --- */
	g_assert_cmpstr (lrg_block_get_value (move, 0), ==, "10");
	g_assert_cmpfloat (lrg_block_get_number (move, "steps"), ==, 10.0);
	g_assert_true (lrg_block_set_value_by_name (move, "steps", "-2.5"));
	g_assert_cmpfloat (lrg_block_get_number (move, "steps"), ==, -2.5);
	g_assert_false (lrg_block_set_value_by_name (move, "nope", "1"));
	v = lrg_block_get_value_variant (move, 0);
	g_assert_true (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE));
	g_assert_cmpfloat (g_variant_get_double (v), ==, -2.5);
	g_clear_pointer (&v, g_variant_unref);
	v = lrg_block_get_value_variant (say, 0);
	g_assert_cmpstr (g_variant_get_string (v, NULL), ==, "Hello");
	g_assert_cmpstr (lrg_block_get_value_by_name (say, "msg"), ==, "Hello");

	/* --- inputs and the type rules --- */
	ife = lrg_block_editor_create_block (e, "if-else", 600.0f, 40.0f);
	xpos = lrg_block_editor_create_block (e, "x-pos", 600.0f, 300.0f);
	touch = lrg_block_editor_create_block (e, "touching", 600.0f, 350.0f);
	g_assert_false (lrg_block_editor_can_attach (e, xpos, ife, LRG_BLOCK_PLACE_INPUT, 0));   /* reporter, boolean slot */
	g_assert_true (lrg_block_editor_can_attach (e, touch, ife, LRG_BLOCK_PLACE_INPUT, 0));
	g_assert_true (lrg_block_editor_can_attach (e, xpos, move, LRG_BLOCK_PLACE_INPUT, 0));
	g_assert_true (lrg_block_editor_can_attach (e, touch, move, LRG_BLOCK_PLACE_INPUT, 0)); /* boolean, number slot */
	g_assert_false (lrg_block_editor_can_attach (e, ife, move, LRG_BLOCK_PLACE_INPUT, 0));  /* statement in a slot */
	g_assert_false (lrg_block_editor_can_attach (e, xpos, move, LRG_BLOCK_PLACE_NEXT, 0));  /* value under a block */
	g_assert_false (lrg_block_editor_can_attach (e, hat, ife, LRG_BLOCK_PLACE_NEXT, 0));    /* hat under a block */
	g_assert_false (lrg_block_editor_can_attach (e, hat, say, LRG_BLOCK_PLACE_NEXT, 0));    /* into itself */
	g_assert_false (lrg_block_editor_can_attach (e, ife, move, LRG_BLOCK_PLACE_MOUTH, 0));  /* no mouth */
	g_assert_true (lrg_block_editor_attach (e, touch, ife, LRG_BLOCK_PLACE_INPUT, 0));
	g_assert_true (lrg_block_editor_attach (e, xpos, move, LRG_BLOCK_PLACE_INPUT, 0));
	g_assert_true (lrg_block_get_input (ife, 0) == touch);
	g_assert_true (lrg_block_get_input_by_name (move, "steps") == xpos);

	/* --- a cap ends a stack: it cannot go above other blocks --- */
	stop = lrg_block_editor_create_block (e, "stop", 800.0f, 40.0f);
	g_assert_false (lrg_block_editor_can_attach (e, stop, hat, LRG_BLOCK_PLACE_NEXT, 0));
	g_assert_true (lrg_block_editor_can_attach (e, stop, say, LRG_BLOCK_PLACE_NEXT, 0));

	/* --- an E block inserted into a stack wraps what was under --- */
	g_assert_true (lrg_block_editor_attach (e, ife, hat, LRG_BLOCK_PLACE_NEXT, 0));
	g_assert_true (lrg_block_get_next (hat) == ife);
	g_assert_true (lrg_block_get_mouth (ife, 0) == move);
	g_assert_null (lrg_block_get_next (ife));

	/* --- removing heals the stack --- */
	say2 = lrg_block_editor_create_block (e, "say", 800.0f, 300.0f);
	g_assert_true (lrg_block_editor_attach (e, say2, move, LRG_BLOCK_PLACE_NEXT, 0));
	g_assert_true (lrg_block_get_next (say2) == rep);   /* inserted between */
	removed = 0;
	lrg_block_editor_remove_block (e, say2, FALSE);
	g_assert_cmpuint (removed, ==, 1);
	g_assert_null (lrg_block_get_editor (say2));
	g_assert_true (lrg_block_get_next (move) == rep);
	removed = 0;
	lrg_block_editor_remove_block (e, move, FALSE);     /* with its input */
	g_assert_cmpuint (removed, ==, 2);
	g_assert_true (lrg_block_get_mouth (ife, 0) == rep);

	/* --- detach makes a script; detach on a top just moves it --- */
	lrg_block_editor_detach (e, rep, 500.0f, 500.0f);
	g_assert_null (lrg_block_get_parent (rep));
	g_assert_null (lrg_block_get_mouth (ife, 0));
	lrg_block_editor_detach (e, rep, 520.0f, 510.0f);
	{
		gfloat x;
		gfloat y;

		lrg_block_get_position (rep, &x, &y);
		g_assert_cmpfloat (x, ==, 520.0f);
		g_assert_cmpfloat (y, ==, 510.0f);
	}

	/* --- kinds in use cannot go --- */
	g_assert_false (lrg_block_editor_remove_kind (e, "repeat"));
	g_assert_true (lrg_block_editor_remove_kind (e, "add"));
	g_assert_null (lrg_block_editor_get_kind (e, "add"));
	g_assert_cmpuint (lrg_block_editor_get_n_kinds (e), ==, 8);

	/* --- walking: scripts list --- */
	{
		GList *scripts = lrg_block_editor_get_scripts (e);

		g_assert_cmpuint (g_list_length (scripts), ==, lrg_block_editor_get_n_scripts (e));
		g_assert_true (scripts->data == lrg_block_editor_get_nth_script (e, 0));
		g_list_free (scripts);
	}

	lrg_block_editor_clear_program (e);
	g_assert_cmpuint (lrg_block_editor_get_n_blocks (e), ==, 0);
	g_assert_true (lrg_block_editor_undo (e));
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), >, 0);
}

/* ==========================================================================
 * Snapping through pointer drags
 * ========================================================================== */

static void
test_snap_statement (Fixture       *f,
                     gconstpointer  data)
{
	LrgBlockEditor *e = f->editor;
	LrgBlock       *hat = lrg_block_editor_create_block (e, "when-flag", 40.0f, 40.0f);
	LrgBlock       *move = lrg_block_editor_create_block (e, "move", 300.0f, 300.0f);
	LrgBlock       *say = lrg_block_editor_create_block (e, "say", 300.0f, 450.0f);
	gfloat          hx;
	gfloat          hy;
	gfloat          hh;
	gfloat          mx;
	gfloat          my;
	gfloat          mh;
	gfloat          sx;
	gfloat          sy;

	settle (f->canvas);
	g_assert_true (lrg_block_editor_get_block_bounds (e, hat, &hx, &hy, NULL, &hh));
	corner (e, move, &mx, &my);

	/* Drop the move block's corner a few units off the hat's bottom */
	drag (f->canvas, mx + 6.0f, my + 6.0f, hx + 6.0f + 9.0f, hy + hh + 6.0f + 7.0f);
	settle (f->canvas);
	g_assert_true (lrg_block_get_parent (move) == hat);
	g_assert_cmpint (lrg_block_get_place (move, NULL), ==, LRG_BLOCK_PLACE_NEXT);
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 2);

	/* Snapped: it sits exactly under the hat */
	g_assert_true (lrg_block_editor_get_block_bounds (e, move, &mx, &my, NULL, &mh));
	g_assert_cmpfloat_with_epsilon (mx, hx, 0.01f);
	g_assert_cmpfloat_with_epsilon (my, hy + hh, 0.01f);

	/* Dragging the hat drags everything below it */
	drag (f->canvas, hx + 10.0f, hy + 10.0f, hx + 110.0f, hy + 60.0f);
	settle (f->canvas);
	g_assert_true (lrg_block_get_next (hat) == move);
	g_assert_true (lrg_block_editor_get_block_bounds (e, move, &mx, &my, NULL, NULL));
	g_assert_cmpfloat_with_epsilon (mx, hx + 100.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (my, hy + hh + 50.0f, 0.01f);

	/* A stack dropped above a script becomes its new top */
	g_assert_true (lrg_block_editor_get_block_bounds (e, say, &sx, &sy, NULL, NULL));
	lrg_block_editor_detach (e, move, 300.0f, 300.0f);
	settle (f->canvas);
	corner (e, move, &mx, &my);
	g_assert_true (lrg_block_editor_get_block_bounds (e, move, NULL, NULL, NULL, &mh));
	drag (f->canvas, mx + 5.0f, my + 5.0f, sx + 5.0f, sy - mh + 5.0f);
	settle (f->canvas);
	g_assert_true (lrg_block_get_next (move) == say);
	g_assert_null (lrg_block_get_parent (move));

	/* Dragging a middle block takes it and the blocks below */
	lrg_block_editor_attach (e, move, hat, LRG_BLOCK_PLACE_NEXT, 0);
	settle (f->canvas);
	corner (e, move, &mx, &my);
	drag (f->canvas, mx + 5.0f, my + 5.0f, mx + 205.0f, my + 205.0f);
	settle (f->canvas);
	g_assert_null (lrg_block_get_next (hat));
	g_assert_null (lrg_block_get_parent (move));
	g_assert_true (lrg_block_get_next (move) == say);
}

static void
test_snap_mouth (Fixture       *f,
                 gconstpointer  data)
{
	LrgBlockEditor *e = f->editor;
	LrgBlock       *rep = lrg_block_editor_create_block (e, "repeat", 40.0f, 40.0f);
	LrgBlock       *move = lrg_block_editor_create_block (e, "move", 300.0f, 300.0f);
	LrgBlock       *say = lrg_block_editor_create_block (e, "say", 300.0f, 450.0f);
	gfloat          rx;
	gfloat          ry;
	gfloat          rh;
	gfloat          mx;
	gfloat          my;
	gfloat          mouth_y;

	settle (f->canvas);
	g_assert_true (lrg_block_editor_get_block_bounds (e, rep, &rx, &ry, NULL, &rh));
	/* An empty mouth is 24 tall above a 22 bottom arm */
	mouth_y = ry + rh - 46.0f;
	corner (e, move, &mx, &my);
	drag (f->canvas, mx + 4.0f, my + 4.0f, rx + 16.0f + 4.0f, mouth_y + 4.0f);
	settle (f->canvas);
	g_assert_true (lrg_block_get_mouth (rep, 0) == move);
	g_assert_cmpint (lrg_block_get_place (move, NULL), ==, LRG_BLOCK_PLACE_MOUTH);

	/* The mouth grew around it */
	{
		gfloat h2;
		gfloat mh;

		g_assert_true (lrg_block_editor_get_block_bounds (e, rep, NULL, NULL, NULL, &h2));
		g_assert_true (lrg_block_editor_get_block_bounds (e, move, &mx, &my, NULL, &mh));
		g_assert_cmpfloat_with_epsilon (h2, rh - 24.0f + mh, 0.01f);
		g_assert_cmpfloat_with_epsilon (mx, rx + 16.0f, 0.01f);
		g_assert_cmpfloat_with_epsilon (my, mouth_y, 0.01f);
	}

	/* A second statement dropped at the mouth goes in above the first */
	corner (e, say, &mx, &my);
	drag (f->canvas, mx + 4.0f, my + 4.0f, rx + 16.0f + 4.0f, mouth_y + 4.0f);
	settle (f->canvas);
	g_assert_true (lrg_block_get_mouth (rep, 0) == say);
	g_assert_true (lrg_block_get_next (say) == move);
}

static void
test_snap_reporter (Fixture       *f,
                    gconstpointer  data)
{
	LrgBlockEditor *e = f->editor;
	LrgBlock       *move = lrg_block_editor_create_block (e, "move", 40.0f, 40.0f);
	LrgBlock       *ife = lrg_block_editor_create_block (e, "if-else", 40.0f, 200.0f);
	LrgBlock       *xpos = lrg_block_editor_create_block (e, "x-pos", 400.0f, 400.0f);
	LrgBlock       *touch = lrg_block_editor_create_block (e, "touching", 400.0f, 500.0f);
	gfloat          sx;
	gfloat          sy;
	gfloat          sh;
	gfloat          bx;
	gfloat          by;
	gfloat          bh;

	settle (f->canvas);

	/* --- a reporter into a number slot: its left middle on the slot's --- */
	g_assert_true (lrg_block_editor_get_slot_bounds (e, move, 0, &sx, &sy, NULL, &sh));
	g_assert_true (lrg_block_editor_get_block_bounds (e, xpos, &bx, &by, NULL, &bh));
	drag (f->canvas, bx + 4.0f, by + bh * 0.5f, sx + 4.0f + 6.0f, sy + sh * 0.5f + 3.0f);
	settle (f->canvas);
	g_assert_true (lrg_block_get_input (move, 0) == xpos);
	g_assert_cmpint (lrg_block_get_place (xpos, NULL), ==, LRG_BLOCK_PLACE_INPUT);

	/* --- type check: a reporter does not go into a boolean slot --- */
	lrg_block_editor_detach (e, xpos, 400.0f, 400.0f);
	settle (f->canvas);
	g_assert_true (lrg_block_editor_get_slot_bounds (e, ife, 0, &sx, &sy, NULL, &sh));
	g_assert_true (lrg_block_editor_get_block_bounds (e, xpos, &bx, &by, NULL, &bh));
	drag (f->canvas, bx + 4.0f, by + bh * 0.5f, sx + 4.0f, sy + sh * 0.5f);
	settle (f->canvas);
	g_assert_null (lrg_block_get_input (ife, 0));
	g_assert_null (lrg_block_get_parent (xpos));

	/* --- a boolean does --- */
	g_assert_true (lrg_block_editor_get_block_bounds (e, touch, &bx, &by, NULL, &bh));
	drag (f->canvas, bx + 4.0f, by + bh * 0.5f, sx + 4.0f, sy + sh * 0.5f);
	settle (f->canvas);
	g_assert_true (lrg_block_get_input (ife, 0) == touch);

	/* --- Escape while carrying puts everything back --- */
	{
		g_autofree gchar *before = program (e);
		g_autofree gchar *after = NULL;

		corner (e, move, &bx, &by);
		move_to (f->canvas, bx + 4.0f, by + 4.0f);
		press_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, bx + 4.0f, by + 4.0f, LRG_INPUT_MODIFIER_NONE);
		hold_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, bx + 60.0f, by + 90.0f);
		hold_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, bx + 120.0f, by + 190.0f);
		key (f->canvas, GRL_KEY_ESCAPE, LRG_INPUT_MODIFIER_NONE);
		release_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, bx + 120.0f, by + 190.0f);
		settle (f->canvas);
		after = program (e);
		g_assert_cmpstr (before, ==, after);
	}
}

/* ==========================================================================
 * Palette drags
 * ========================================================================== */

static void
test_palette_drag (Fixture       *f,
                   gconstpointer  data)
{
	LrgBlockEditor *e = f->editor;
	LrgBlock       *made = NULL;
	LrgBlock       *gone = NULL;
	LrgBlock       *top;
	gfloat          px;
	gfloat          py;
	gfloat          wx;
	gfloat          wy;
	guint           added = 0;
	guint           removed = 0;
	guint           changed = 0;

	g_signal_connect (e, "block-added", G_CALLBACK (last_block_cb), &made);
	g_signal_connect (e, "block-removed", G_CALLBACK (last_block_cb), &gone);
	g_signal_connect_swapped (e, "block-added", G_CALLBACK (count_cb), &added);
	g_signal_connect_swapped (e, "block-removed", G_CALLBACK (count_cb), &removed);
	g_signal_connect_swapped (e, "changed", G_CALLBACK (count_cb), &changed);

	/* --- drag from the palette creates a block --- */
	g_assert_true (lrg_block_editor_get_palette_block_bounds (e, "say", &px, &py, NULL, NULL));
	lrg_block_editor_get_workspace_bounds (e, &wx, &wy, NULL, NULL);
	g_assert_cmpfloat (px, <, wx);
	drag (f->canvas, px + 6.0f, py + 6.0f, wx + 306.0f, wy + 206.0f);
	settle (f->canvas);
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 1);
	g_assert_cmpuint (added, ==, 1);
	g_assert_cmpuint (changed, ==, 1);
	top = lrg_block_editor_get_nth_script (e, 0);
	g_assert_true (made == top);
	g_assert_cmpstr (lrg_block_get_kind_id (top), ==, "say");
	{
		gfloat x;
		gfloat y;

		lrg_block_get_position (top, &x, &y);
		g_assert_cmpfloat_with_epsilon (x, 300.0f, 0.01f);
		g_assert_cmpfloat_with_epsilon (y, 200.0f, 0.01f);
	}

	/* --- a click on the palette (no drag) creates nothing --- */
	click_at (f->canvas, px + 6.0f, py + 6.0f);
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 1);

	/* --- dragging a script onto the palette deletes it --- */
	lrg_block_editor_attach (e, lrg_block_editor_create_block (e, "move", 0.0f, 0.0f), top,
	                         LRG_BLOCK_PLACE_NEXT, 0);
	settle (f->canvas);
	removed = 0;
	corner (e, top, &wx, &wy);
	drag (f->canvas, wx + 5.0f, wy + 5.0f, px + 40.0f, py + 100.0f);
	settle (f->canvas);
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 0);
	g_assert_cmpuint (lrg_block_editor_get_n_blocks (e), ==, 0);
	g_assert_cmpuint (removed, ==, 2);
	g_assert_nonnull (gone);

	/* --- undo brings the very same blocks back --- */
	g_assert_true (lrg_block_editor_undo (e));
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 1);
	g_assert_true (lrg_block_editor_get_nth_script (e, 0) == top);
	g_assert_true (lrg_block_get_editor (top) == e);

	/* --- the category strip scrolls the palette --- */
	g_assert_true (lrg_block_editor_scroll_palette_to (e, "sensing"));
	g_assert_false (lrg_block_editor_scroll_palette_to (e, "nope"));
}

/* ==========================================================================
 * Undo and redo of every operation
 * ========================================================================== */

typedef void (*EditFunc) (Fixture *f, gpointer data);

/* check_undo: @edit changes the program; undo restores it exactly, redo
 * re-applies it exactly, and undo again restores it again */
static void
check_undo (Fixture  *f,
            EditFunc  edit,
            gpointer  data)
{
	g_autofree gchar *before = NULL;
	g_autofree gchar *after = NULL;
	g_autofree gchar *undone = NULL;
	g_autofree gchar *redone = NULL;

	settle (f->canvas);
	before = program (f->editor);
	edit (f, data);
	settle (f->canvas);
	after = program (f->editor);
	g_assert_cmpstr (before, !=, after);
	g_assert_true (lrg_block_editor_can_undo (f->editor));
	g_assert_true (lrg_block_editor_undo (f->editor));
	settle (f->canvas);
	undone = program (f->editor);
	g_assert_cmpstr (before, ==, undone);
	g_assert_true (lrg_block_editor_can_redo (f->editor));
	g_assert_true (lrg_block_editor_redo (f->editor));
	settle (f->canvas);
	redone = program (f->editor);
	g_assert_cmpstr (after, ==, redone);
	g_assert_true (lrg_block_editor_undo (f->editor));
	g_free (undone);
	undone = program (f->editor);
	g_assert_cmpstr (before, ==, undone);
	g_assert_true (lrg_block_editor_redo (f->editor));
}

static LrgBlock *
script (Fixture *f,
        guint    i)
{
	return lrg_block_editor_get_nth_script (f->editor, i);
}

static void
op_create (Fixture  *f,
           gpointer  data)
{
	lrg_block_editor_create_block (f->editor, "move", 500.0f, 500.0f);
}

static void
op_attach (Fixture  *f,
           gpointer  data)
{
	g_assert_true (lrg_block_editor_attach (f->editor, script (f, 1), script (f, 0), LRG_BLOCK_PLACE_NEXT, 0));
}

static void
op_value (Fixture  *f,
          gpointer  data)
{
	lrg_block_set_value (lrg_block_get_next (data), 0, "77");
}

static void
op_remove (Fixture  *f,
           gpointer  data)
{
	lrg_block_editor_remove_block (f->editor, lrg_block_get_next (data), FALSE);
}

static void
op_drag (Fixture  *f,
         gpointer  data)
{
	gfloat x;
	gfloat y;

	corner (f->editor, script (f, 0), &x, &y);
	drag (f->canvas, x + 5.0f, y + 5.0f, x + 155.0f, y + 85.0f);
}

static void
op_palette (Fixture  *f,
            gpointer  data)
{
	gfloat px;
	gfloat py;

	g_assert_true (lrg_block_editor_get_palette_block_bounds (f->editor, "x-pos", &px, &py, NULL, NULL));
	drag (f->canvas, px + 5.0f, py + 5.0f, 800.0f, 600.0f);
}

static void
op_trash (Fixture  *f,
          gpointer  data)
{
	gfloat x;
	gfloat y;

	corner (f->editor, script (f, 0), &x, &y);
	drag (f->canvas, x + 5.0f, y + 5.0f, 100.0f, 300.0f);
}

static void
op_clean_up (Fixture  *f,
             gpointer  data)
{
	lrg_block_editor_clean_up (f->editor);
}

static void
op_duplicate (Fixture  *f,
              gpointer  data)
{
	lrg_block_editor_select_block (f->editor, script (f, 0));
	g_assert_nonnull (lrg_block_editor_duplicate_selection (f->editor));
}

static void
op_paste (Fixture  *f,
          gpointer  data)
{
	g_assert_nonnull (lrg_block_editor_paste (f->editor));
}

static void
op_slot_typing (Fixture  *f,
                gpointer  data)
{
	g_assert_true (lrg_block_editor_begin_slot_edit (f->editor, lrg_block_get_next (data), 0));
	type_text (f->canvas, "123");
	key (f->canvas, GRL_KEY_ENTER, LRG_INPUT_MODIFIER_NONE);
}

static void
op_clear (Fixture  *f,
          gpointer  data)
{
	lrg_block_editor_clear_program (f->editor);
}

static void
test_undo_redo (Fixture       *f,
                gconstpointer  data)
{
	LrgBlockEditor *e = f->editor;
	LrgBlock       *hat = lrg_block_editor_create_block (e, "when-flag", 40.0f, 40.0f);
	LrgBlock       *move;
	LrgBlock       *orphan;

	lrg_block_editor_create_block (e, "move", 300.0f, 300.0f);
	check_undo (f, op_create, NULL);
	lrg_block_editor_undo (e);   /* (drop the extra block again) */
	check_undo (f, op_attach, NULL);
	move = lrg_block_get_next (hat);
	g_assert_nonnull (move);
	check_undo (f, op_value, hat);
	check_undo (f, op_slot_typing, hat);
	check_undo (f, op_drag, NULL);
	check_undo (f, op_palette, NULL);
	check_undo (f, op_clean_up, NULL);
	check_undo (f, op_duplicate, NULL);
	lrg_block_editor_select_block (e, hat);
	g_assert_true (lrg_block_editor_copy_selection (e));
	check_undo (f, op_paste, NULL);

	/* Identity survives undo of a removal */
	check_undo (f, op_remove, hat);
	lrg_block_editor_undo (e);
	g_assert_true (lrg_block_get_next (hat) == move);
	g_assert_true (lrg_block_get_editor (move) == e);
	lrg_block_editor_redo (e);
	g_assert_null (lrg_block_get_editor (move));
	lrg_block_editor_undo (e);

	check_undo (f, op_trash, NULL);
	check_undo (f, op_clear, NULL);

	/* --- keyboard undo / redo --- */
	orphan = lrg_block_editor_create_block (e, "say", 600.0f, 600.0f);
	g_assert_true (lrg_block_get_editor (orphan) == e);
	key (f->canvas, GRL_KEY_Z, LRG_INPUT_MODIFIER_CTRL);
	g_assert_null (lrg_block_get_editor (orphan));
	key (f->canvas, GRL_KEY_Z, LRG_INPUT_MODIFIER_CTRL | LRG_INPUT_MODIFIER_SHIFT);
	g_assert_true (lrg_block_get_editor (orphan) == e);
	key (f->canvas, GRL_KEY_Z, LRG_INPUT_MODIFIER_CTRL);
	key (f->canvas, GRL_KEY_Y, LRG_INPUT_MODIFIER_CTRL);
	g_assert_true (lrg_block_get_editor (orphan) == e);

	/* --- the limit and clearing --- */
	lrg_block_editor_set_undo_limit (e, 2);
	g_assert_cmpuint (lrg_block_editor_get_undo_limit (e), ==, 2);
	g_assert_true (lrg_block_editor_undo (e));
	g_assert_true (lrg_block_editor_undo (e));
	g_assert_false (lrg_block_editor_undo (e));
	lrg_block_editor_clear_history (e);
	g_assert_false (lrg_block_editor_can_redo (e));
}

/* ==========================================================================
 * YAML
 * ========================================================================== */

static void
build_rich_program (LrgBlockEditor *e)
{
	LrgBlock *hat = lrg_block_editor_create_block (e, "when-flag", 40.0f, 40.5f);
	LrgBlock *rep = lrg_block_editor_create_block (e, "repeat", 0.0f, 0.0f);
	LrgBlock *ife = lrg_block_editor_create_block (e, "if-else", 0.0f, 0.0f);
	LrgBlock *say = lrg_block_editor_create_block (e, "say", 0.0f, 0.0f);
	LrgBlock *say2 = lrg_block_editor_create_block (e, "say", 0.0f, 0.0f);
	LrgBlock *move = lrg_block_editor_create_block (e, "move", 0.0f, 0.0f);
	LrgBlock *add = lrg_block_editor_create_block (e, "add", 0.0f, 0.0f);
	LrgBlock *xpos = lrg_block_editor_create_block (e, "x-pos", 0.0f, 0.0f);
	LrgBlock *touch = lrg_block_editor_create_block (e, "touching", 0.0f, 0.0f);
	LrgBlock *stop = lrg_block_editor_create_block (e, "stop", 0.0f, 0.0f);
	GrlColor  pink = { 0xf3, 0x8b, 0xa8, 0xff };

	g_assert_true (lrg_block_editor_attach (e, rep, hat, LRG_BLOCK_PLACE_NEXT, 0));
	g_assert_true (lrg_block_editor_attach (e, ife, rep, LRG_BLOCK_PLACE_MOUTH, 0));
	g_assert_true (lrg_block_editor_attach (e, say, ife, LRG_BLOCK_PLACE_MOUTH, 0));
	g_assert_true (lrg_block_editor_attach (e, say2, ife, LRG_BLOCK_PLACE_MOUTH, 1));
	g_assert_true (lrg_block_editor_attach (e, move, ife, LRG_BLOCK_PLACE_NEXT, 0));
	g_assert_true (lrg_block_editor_attach (e, add, move, LRG_BLOCK_PLACE_INPUT, 0));
	g_assert_true (lrg_block_editor_attach (e, xpos, add, LRG_BLOCK_PLACE_INPUT, 1));
	g_assert_true (lrg_block_editor_attach (e, touch, ife, LRG_BLOCK_PLACE_INPUT, 0));
	g_assert_true (lrg_block_editor_attach (e, stop, rep, LRG_BLOCK_PLACE_NEXT, 0));
	lrg_block_set_value (say, 0, "");
	lrg_block_set_value (say2, 0, "null");
	lrg_block_set_value (add, 0, "0.1");
	lrg_block_set_value_by_name (stop, "what", "this script");
	lrg_block_set_value_by_name (touch, "what", "mouse");
	lrg_block_editor_create_block (e, "say", -20.25f, 300.0f);
	lrg_block_set_value (lrg_block_editor_get_nth_script (e, 1), 0, "quote: \"x\" {y} | z");
	lrg_block_editor_create_block (e, "x-pos", 500.0f, 500.0f);
	lrg_block_kind_set_color (lrg_block_editor_get_kind (e, "say"), &pink);
	lrg_block_editor_set_category_color (e, "motion", &(GrlColor){ 0x12, 0x34, 0x56, 0xff });
	{
		const gchar *who[] = { "edge", "mouse", "player one", NULL };

		lrg_block_kind_set_slot_choices (lrg_block_editor_get_kind (e, "touching"), 0, who);
	}
	lrg_block_editor_set_scroll (e, -12.5f, 30.0f);
	lrg_block_editor_set_zoom (e, 1.25f);
}

static void
test_yaml (Fixture       *f,
           gconstpointer  data)
{
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgBlockEditor) copy = lrg_block_editor_new ();
	g_autofree gchar         *yaml = NULL;
	g_autofree gchar         *again = NULL;
	g_autofree gchar         *prog = NULL;
	g_autofree gchar         *dir = NULL;
	g_autofree gchar         *path = NULL;
	LrgBlock                 *hat;
	LrgBlock                 *ife;

	build_rich_program (f->editor);
	yaml = lrg_block_editor_save_yaml (f->editor, LRG_BLOCK_YAML_ALL, NULL, &error);
	g_assert_no_error (error);

	/* --- the round trip is exact --- */
	g_assert_true (lrg_block_editor_load_yaml (copy, yaml, -1, &error));
	g_assert_no_error (error);
	again = lrg_block_editor_save_yaml (copy, LRG_BLOCK_YAML_ALL, NULL, &error);
	g_assert_cmpstr (yaml, ==, again);

	/* --- and the structure came back --- */
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (copy), ==, 3);
	g_assert_cmpuint (lrg_block_editor_get_n_blocks (copy), ==, lrg_block_editor_get_n_blocks (f->editor));
	hat = lrg_block_editor_get_nth_script (copy, 0);
	ife = lrg_block_get_mouth (lrg_block_get_next (hat), 0);
	g_assert_cmpstr (lrg_block_get_kind_id (ife), ==, "if-else");
	g_assert_cmpstr (lrg_block_get_value (lrg_block_get_mouth (ife, 0), 0), ==, "");
	g_assert_cmpstr (lrg_block_get_value (lrg_block_get_mouth (ife, 1), 0), ==, "null");
	g_assert_cmpstr (lrg_block_get_kind_id (lrg_block_get_input (lrg_block_get_input (lrg_block_get_next (ife), 0), 1)),
	                 ==, "x-pos");
	g_assert_cmpstr (lrg_block_get_value (lrg_block_editor_get_nth_script (copy, 1), 0), ==, "quote: \"x\" {y} | z");
	g_assert_cmpstr (lrg_block_kind_get_slot_choices (lrg_block_editor_get_kind (copy, "touching"), 0)[2], ==,
	                 "player one");
	g_assert_cmpfloat (lrg_block_editor_get_zoom (copy), ==, 1.25f);
	g_assert_cmpuint (lrg_block_editor_can_undo (copy), ==, FALSE);

	/* --- a palette-only reload keeps the program --- */
	prog = program (copy);
	g_clear_pointer (&again, g_free);
	again = lrg_block_editor_save_yaml (f->editor, LRG_BLOCK_YAML_PALETTE, NULL, &error);
	g_assert_true (lrg_block_editor_load_yaml (copy, again, -1, &error));
	g_assert_no_error (error);
	g_clear_pointer (&again, g_free);
	again = program (copy);
	g_assert_cmpstr (prog, ==, again);

	/* --- bad documents leave the editor untouched --- */
	{
		static const gchar * const bad[] = {
			"scripts: [ { x: 0, y: 0, blocks: [ { kind: nope } ] } ]",
			"scripts: [ { x: 0, y: 0, blocks: [ { kind: if-else, inputs: { cond: { kind: x-pos } } } ] } ]",
			"scripts: [ { x: 0, y: 0, blocks: [ { kind: move }, { kind: when-flag } ] } ]",
			"scripts: [ { x: 0, y: 0, blocks: [ { kind: stop }, { kind: move } ] } ]",
			"scripts: [ { x: 0, y: 0, blocks: [ { kind: move, values: { nope: 1 } } ] } ]",
			"scripts: [ { x: 0, y: 0, blocks: [ { kind: repeat, mouths: [ [ { kind: x-pos } ] ] } ] } ]",
			"scripts: [ { x: 0, y: 0, blocks: [] } ]",
			"kinds: [ { id: a, shape: statement, label: 'x {' } ]",
			"kinds: [ { id: move, shape: statement, label: move } ]",
			"version: 2",
			"- not a mapping",
			"scripts: { oops: 1 }"
		};
		guint i;

		for (i = 0; i < G_N_ELEMENTS (bad); i++)
		{
			g_assert_false (lrg_block_editor_load_yaml (copy, bad[i], -1, &error));
			g_assert_true (error != NULL && error->domain == LRG_BLOCK_EDITOR_ERROR);
			g_clear_error (&error);
			g_clear_pointer (&again, g_free);
			again = program (copy);
			g_assert_cmpstr (prog, ==, again);
		}
	}

	/* --- a hand-written document with defaults --- */
	g_assert_true (lrg_block_editor_load_yaml (copy,
	                                           "scripts:\n"
	                                           "  - blocks:\n"
	                                           "      - kind: when-flag\n"
	                                           "      - kind: move\n"
	                                           "        values: { steps: 5 }\n", -1, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (copy), ==, 1);
	g_assert_cmpfloat (lrg_block_get_number (lrg_block_get_next (lrg_block_editor_get_nth_script (copy, 0)),
	                                         "steps"), ==, 5.0);

	/* --- files --- */
	dir = g_dir_make_tmp ("lrg-block-editor-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "program.yaml", NULL);
	g_assert_true (lrg_block_editor_save_yaml_to_file (f->editor, LRG_BLOCK_YAML_ALL, path, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_block_editor_load_yaml_from_file (copy, path, &error));
	g_assert_no_error (error);
	g_clear_pointer (&again, g_free);
	again = lrg_block_editor_save_yaml (copy, LRG_BLOCK_YAML_ALL, NULL, &error);
	g_assert_cmpstr (yaml, ==, again);
	g_assert_cmpint (g_remove (path), ==, 0);
	g_assert_false (lrg_block_editor_load_yaml_from_file (copy, path, &error));
	g_assert_error (error, LRG_BLOCK_EDITOR_ERROR, LRG_BLOCK_EDITOR_ERROR_IO);
	g_clear_error (&error);
	g_assert_cmpint (g_rmdir (dir), ==, 0);
}

/* ==========================================================================
 * View maths, panning and zooming
 * ========================================================================== */

static void
test_view (Fixture       *f,
           gconstpointer  data)
{
	LrgBlockEditor *e = f->editor;
	gfloat          wx;
	gfloat          wy;
	gfloat          ww;
	gfloat          wh;
	gfloat          sx;
	gfloat          sy;
	gfloat          x;
	gfloat          y;
	gfloat          px;
	gfloat          pw;

	/* --- areas: strip + palette on the left of the workspace --- */
	lrg_block_editor_get_workspace_bounds (e, &wx, &wy, &ww, &wh);
	lrg_block_editor_get_palette_bounds (e, &px, NULL, &pw, NULL);
	g_assert_cmpfloat (px, ==, 0.0f);
	g_assert_cmpfloat (pw, ==, 64.0f + 220.0f);
	g_assert_cmpfloat (wx, ==, 284.0f);
	g_assert_cmpfloat (ww, ==, 1000.0f - 284.0f);

	/* --- mapping and its inverse --- */
	lrg_block_editor_workspace_to_screen (e, 0.0f, 0.0f, &sx, &sy);
	g_assert_cmpfloat (sx, ==, wx);
	g_assert_cmpfloat (sy, ==, wy);
	lrg_block_editor_set_scroll (e, 100.0f, 50.0f);
	lrg_block_editor_set_zoom (e, 2.0f);
	lrg_block_editor_workspace_to_screen (e, 110.0f, 60.0f, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (sx, wx + 20.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (sy, wy + 20.0f, 0.001f);
	lrg_block_editor_screen_to_workspace (e, sx, sy, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 110.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (y, 60.0f, 0.001f);

	/* --- zoom about a point keeps it fixed --- */
	lrg_block_editor_screen_to_workspace (e, 600.0f, 400.0f, &x, &y);
	lrg_block_editor_zoom_at (e, 600.0f, 400.0f, 0.75f);
	g_assert_cmpfloat (lrg_block_editor_get_zoom (e), ==, 0.75f);
	lrg_block_editor_workspace_to_screen (e, x, y, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (sx, 600.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (sy, 400.0f, 0.01f);

	/* --- range clamps --- */
	lrg_block_editor_zoom_at (e, 600.0f, 400.0f, 100.0f);
	g_assert_cmpfloat (lrg_block_editor_get_zoom (e), ==, lrg_block_editor_get_max_zoom (e));
	lrg_block_editor_set_zoom_range (e, 0.5f, 2.0f);
	g_assert_cmpfloat (lrg_block_editor_get_zoom (e), ==, 2.0f);
	g_assert_cmpfloat (lrg_block_editor_get_min_zoom (e), ==, 0.5f);
	lrg_block_editor_set_zoom (e, 1.0f);

	/* --- Ctrl + wheel zooms about the pointer --- */
	lrg_block_editor_screen_to_workspace (e, 700.0f, 300.0f, &x, &y);
	wheel (f->canvas, 700.0f, 300.0f, 1.0f, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpfloat (lrg_block_editor_get_zoom (e), >, 1.0f);
	lrg_block_editor_workspace_to_screen (e, x, y, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (sx, 700.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (sy, 300.0f, 0.01f);

	/* --- the wheel alone scrolls --- */
	lrg_block_editor_set_zoom (e, 1.0f);
	lrg_block_editor_get_scroll (e, &x, &y);
	wheel (f->canvas, 700.0f, 300.0f, -1.0f, LRG_INPUT_MODIFIER_NONE);
	lrg_block_editor_get_scroll (e, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (sy, y + 48.0f, 0.01f);
	g_assert_cmpfloat (sx, ==, x);

	/* --- drags on empty workspace pan: left, middle, right, Space + left --- */
	{
		GrlMouseButton buttons[] = { GRL_MOUSE_BUTTON_LEFT, GRL_MOUSE_BUTTON_MIDDLE, GRL_MOUSE_BUTTON_RIGHT };
		guint          i;

		for (i = 0; i < G_N_ELEMENTS (buttons); i++)
		{
			lrg_block_editor_get_scroll (e, &x, &y);
			drag_with (f->canvas, buttons[i], 600.0f, 400.0f, 640.0f, 380.0f);
			lrg_block_editor_get_scroll (e, &sx, &sy);
			g_assert_cmpfloat_with_epsilon (sx, x - 40.0f, 0.01f);
			g_assert_cmpfloat_with_epsilon (sy, y + 20.0f, 0.01f);
		}
	}

	/* --- right-to-left: the palette moves to the right --- */
	lrg_widget_set_direction (LRG_WIDGET (e), LRG_WIDGET_DIRECTION_RTL);
	lrg_block_editor_get_workspace_bounds (e, &wx, NULL, &ww, NULL);
	lrg_block_editor_get_palette_bounds (e, &px, NULL, &pw, NULL);
	g_assert_cmpfloat (wx, ==, 0.0f);
	g_assert_cmpfloat (px, ==, ww);
	g_assert_cmpfloat (px + pw, ==, 1000.0f);
	g_assert_true (lrg_block_editor_get_palette_block_bounds (e, "move", &x, NULL, NULL, NULL));
	g_assert_cmpfloat (x, >=, px);

	/* --- hidden palette: the workspace fills the widget --- */
	lrg_block_editor_set_show_palette (e, FALSE);
	lrg_block_editor_get_workspace_bounds (e, &wx, NULL, &ww, NULL);
	g_assert_cmpfloat (ww, ==, 1000.0f);
	g_assert_false (lrg_block_editor_get_palette_block_bounds (e, "move", NULL, NULL, NULL, NULL));
}

/* ==========================================================================
 * Slot editing and dropdowns
 * ========================================================================== */

static void
test_slot_editing (Fixture       *f,
                   gconstpointer  data)
{
	LrgBlockEditor *e = f->editor;
	LrgBlock       *move = lrg_block_editor_create_block (e, "move", 40.0f, 40.0f);
	LrgBlock       *say = lrg_block_editor_create_block (e, "say", 40.0f, 200.0f);
	LrgBlock       *stop = lrg_block_editor_create_block (e, "stop", 40.0f, 350.0f);
	LrgBlock       *editing = NULL;
	guint           slot = 9;
	gfloat          sx;
	gfloat          sy;
	gfloat          sw;
	gfloat          sh;
	gfloat          w0;

	settle (f->canvas);

	/* --- click a number slot, type over it, Enter keeps it --- */
	g_assert_true (lrg_block_editor_get_slot_bounds (e, move, 0, &sx, &sy, &w0, &sh));
	click_at (f->canvas, sx + w0 * 0.5f, sy + sh * 0.5f);
	g_assert_true (lrg_block_editor_get_edit_slot (e, &editing, &slot));
	g_assert_true (editing == move);
	g_assert_cmpuint (slot, ==, 0);
	g_assert_cmpstr (lrg_block_editor_get_edit_text (e), ==, "10");
	type_text (f->canvas, "4x2");          /* letters are not numbers */
	g_assert_cmpstr (lrg_block_editor_get_edit_text (e), ==, "42");
	type_text (f->canvas, "0000");
	settle (f->canvas);
	g_assert_true (lrg_block_editor_get_slot_bounds (e, move, 0, NULL, NULL, &sw, NULL));
	g_assert_cmpfloat (sw, >, w0);         /* the field grows with its text */
	key (f->canvas, GRL_KEY_BACKSPACE, LRG_INPUT_MODIFIER_NONE);
	key (f->canvas, GRL_KEY_LEFT, LRG_INPUT_MODIFIER_NONE);
	key (f->canvas, GRL_KEY_DELETE, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpstr (lrg_block_editor_get_edit_text (e), ==, "4200");
	key (f->canvas, GRL_KEY_HOME, LRG_INPUT_MODIFIER_NONE);
	type_text (f->canvas, "-");
	key (f->canvas, GRL_KEY_ENTER, LRG_INPUT_MODIFIER_NONE);
	g_assert_false (lrg_block_editor_get_edit_slot (e, NULL, NULL));
	g_assert_cmpstr (lrg_block_get_value (move, 0), ==, "-4200");

	/* --- an invalid number reverts, Escape cancels --- */
	g_assert_true (lrg_block_editor_begin_slot_edit (e, move, 0));
	type_text (f->canvas, "--");
	key (f->canvas, GRL_KEY_ENTER, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpstr (lrg_block_get_value (move, 0), ==, "-4200");
	g_assert_true (lrg_block_editor_begin_slot_edit (e, move, 0));
	type_text (f->canvas, "7");
	key (f->canvas, GRL_KEY_ESCAPE, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpstr (lrg_block_get_value (move, 0), ==, "-4200");

	/* --- text slots take anything, Space included; clicking away keeps it --- */
	g_assert_true (lrg_block_editor_get_slot_bounds (e, say, 0, &sx, &sy, &sw, &sh));
	click_at (f->canvas, sx + sw * 0.5f, sy + sh * 0.5f);
	type_text (f->canvas, "Hi there");
	click_at (f->canvas, 900.0f, 650.0f);
	g_assert_cmpstr (lrg_block_get_value (say, 0), ==, "Hi there");

	/* --- Tab: from a selected block into its first slot --- */
	lrg_block_editor_select_block (e, move);
	key (f->canvas, GRL_KEY_TAB, LRG_INPUT_MODIFIER_NONE);
	g_assert_true (lrg_block_editor_get_edit_slot (e, &editing, NULL));
	g_assert_true (editing == move);
	lrg_block_editor_cancel_slot_edit (e);

	/* --- a dropdown opens a menu; picking a row sets the value --- */
	g_assert_true (lrg_block_editor_get_slot_bounds (e, stop, 0, &sx, &sy, &sw, &sh));
	click_at (f->canvas, sx + sw * 0.5f, sy + sh * 0.5f);
	{
		LrgWidget *popup = lrg_canvas_get_top_popup (f->canvas);

		g_assert_true (LRG_IS_MENU (popup));
		g_assert_cmpuint (lrg_menu_get_n_entries (LRG_MENU (popup)), ==, 2);
		g_assert_cmpstr (lrg_menu_get_entry_label (LRG_MENU (popup), 1), ==, "this script");
		g_assert_true (lrg_menu_activate_entry (LRG_MENU (popup), 1));
	}
	g_assert_cmpstr (lrg_block_get_value (stop, 0), ==, "this script");
	g_assert_true (lrg_block_editor_undo (e));
	g_assert_cmpstr (lrg_block_get_value (stop, 0), ==, "all");

	/* --- boolean holes are not typed into --- */
	{
		LrgBlock *ife = lrg_block_editor_create_block (e, "if-else", 400.0f, 40.0f);

		g_assert_false (lrg_block_editor_begin_slot_edit (e, ife, 0));
	}
}

/* ==========================================================================
 * Keyboard and activation
 * ========================================================================== */

static void
test_keyboard (Fixture       *f,
               gconstpointer  data)
{
	LrgBlockEditor *e = f->editor;
	LrgBlock       *hat = lrg_block_editor_create_block (e, "when-flag", 40.0f, 40.0f);
	LrgBlock       *move = lrg_block_editor_create_block (e, "move", 0.0f, 0.0f);
	LrgBlock       *say = lrg_block_editor_create_block (e, "say", 0.0f, 0.0f);
	LrgBlock       *other = lrg_block_editor_create_block (e, "repeat", 400.0f, 40.0f);
	LrgBlock       *activated = NULL;
	guint           selections = 0;
	gfloat          x;
	gfloat          y;

	g_signal_connect (e, "block-activated", G_CALLBACK (last_block_cb), &activated);
	g_signal_connect_swapped (e, "selection-changed", G_CALLBACK (count_cb), &selections);
	lrg_block_editor_attach (e, move, hat, LRG_BLOCK_PLACE_NEXT, 0);
	lrg_block_editor_attach (e, say, move, LRG_BLOCK_PLACE_NEXT, 0);
	settle (f->canvas);

	/* --- arrows walk the script and the scripts --- */
	key (f->canvas, GRL_KEY_DOWN, LRG_INPUT_MODIFIER_NONE);
	g_assert_true (lrg_block_editor_get_selected (e) == hat);
	g_assert_true (lrg_block_get_selected (hat));
	key (f->canvas, GRL_KEY_DOWN, LRG_INPUT_MODIFIER_NONE);
	g_assert_true (lrg_block_editor_get_selected (e) == move);
	key (f->canvas, GRL_KEY_UP, LRG_INPUT_MODIFIER_NONE);
	g_assert_true (lrg_block_editor_get_selected (e) == hat);
	key (f->canvas, GRL_KEY_RIGHT, LRG_INPUT_MODIFIER_NONE);
	g_assert_true (lrg_block_editor_get_selected (e) == other);
	key (f->canvas, GRL_KEY_LEFT, LRG_INPUT_MODIFIER_NONE);
	g_assert_true (lrg_block_editor_get_selected (e) == hat);
	g_assert_cmpuint (selections, >=, 5);

	/* --- Enter runs the selected script --- */
	lrg_block_editor_select_block (e, say);
	key (f->canvas, GRL_KEY_ENTER, LRG_INPUT_MODIFIER_NONE);
	g_assert_true (activated == hat);

	/* --- double click on the hat runs it too --- */
	activated = NULL;
	corner (e, hat, &x, &y);
	click_at (f->canvas, x + 8.0f, y + 8.0f);
	g_assert_null (activated);
	click_at (f->canvas, x + 8.0f, y + 8.0f);
	g_assert_true (activated == hat);

	/* --- Delete removes the selection; the stack closes up --- */
	lrg_block_editor_select_block (e, move);
	key (f->canvas, GRL_KEY_DELETE, LRG_INPUT_MODIFIER_NONE);
	g_assert_null (lrg_block_get_editor (move));
	g_assert_null (lrg_block_editor_get_selected (e));
	g_assert_true (lrg_block_get_next (hat) == say);

	/* --- Ctrl+D duplicates the selected stack beside it --- */
	lrg_block_editor_select_block (e, hat);
	key (f->canvas, GRL_KEY_D, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 3);
	{
		LrgBlock *copy = lrg_block_editor_get_selected (e);

		g_assert_true (copy != hat);
		g_assert_cmpstr (lrg_block_get_kind_id (copy), ==, "when-flag");
		g_assert_cmpstr (lrg_block_get_kind_id (lrg_block_get_next (copy)), ==, "say");
	}

	/* --- Ctrl+C / Ctrl+V; Ctrl+X cuts --- */
	lrg_block_editor_select_block (e, say);
	key (f->canvas, GRL_KEY_C, LRG_INPUT_MODIFIER_CTRL);
	move_to (f->canvas, 800.0f, 500.0f);
	key (f->canvas, GRL_KEY_V, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 4);
	g_assert_cmpstr (lrg_block_get_kind_id (lrg_block_editor_get_selected (e)), ==, "say");
	key (f->canvas, GRL_KEY_X, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 3);

	/* --- the actions do the same --- */
	lrg_block_editor_select_block (e, other);
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (e), "blocks.duplicate", NULL));
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 4);
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (e), "blocks.undo", NULL));
	g_assert_cmpuint (lrg_block_editor_get_n_scripts (e), ==, 3);
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (e), "blocks.clean-up", NULL));

	/* --- Escape clears the selection --- */
	lrg_block_editor_select_block (e, other);
	key (f->canvas, GRL_KEY_ESCAPE, LRG_INPUT_MODIFIER_NONE);
	g_assert_null (lrg_block_editor_get_selected (e));
}

/* ==========================================================================
 * Painting (headless)
 * ========================================================================== */

/* has_color: some instance of @prim is drawn in exactly @c */
static gboolean
has_color (LrgPaintList   *list,
           LrgPaintPrim    prim,
           const GrlColor *c)
{
	guint i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		if (lrg_paint_instance_get_prim (inst) == prim && inst->color[0] == c->r && inst->color[1] == c->g &&
		    inst->color[2] == c->b)
			return TRUE;
	}
	return FALSE;
}

static void
test_paint (Fixture       *f,
            gconstpointer  data)
{
	LrgBlockEditor      *e = f->editor;
	const gchar * const *presets = lrg_theme_list_presets ();
	g_autoptr(LrgTheme)  theme = lrg_theme_new_preset ("midnight");
	LrgPaintList        *list;
	LrgBlock            *hat;
	GrlColor             motion;
	GrlColor             glow = { 255, 214, 64, 255 };
	guint                empty;
	guint                full;
	guint                i;

	lrg_widget_set_theme (LRG_WIDGET (f->canvas), theme);
	list = lrg_canvas_record (f->canvas);
	empty = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (empty, >, 20);    /* palette, strip, grid */

	build_rich_program (e);
	lrg_block_editor_set_zoom (e, 1.0f);
	lrg_block_editor_set_scroll (e, -10.0f, -10.0f);
	settle (f->canvas);
	list = lrg_canvas_record (f->canvas);
	full = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (full, >, empty + 60);

	/* Motion blocks are drawn in the category's colour (their bottom
	 * tabs are flat triangles of the gradient's bottom shade) */
	lrg_block_editor_get_category_color (e, "motion", &motion);
	g_assert_cmpuint (motion.r, ==, 0x12);
	lrg_paint_color_shade (&motion, -0.05f, &motion);
	g_assert_true (has_color (list, LRG_PAINT_PRIM_TRIANGLE, &motion));

	/* A running script glows */
	hat = lrg_block_editor_get_nth_script (e, 0);
	g_assert_false (has_color (list, LRG_PAINT_PRIM_SHADOW, &glow));
	lrg_block_set_running (hat, TRUE);
	g_assert_true (lrg_block_get_running (hat));
	settle (f->canvas);
	list = lrg_canvas_record (f->canvas);
	g_assert_true (has_color (list, LRG_PAINT_PRIM_SHADOW, &glow));
	lrg_block_editor_clear_running (e);
	g_assert_false (lrg_block_get_running (hat));

	/* A drag shows the insertion ghost and the gap opens */
	{
		LrgBlock *say = lrg_block_editor_create_block (e, "say", 600.0f, 600.0f);
		LrgBlock *stop = lrg_block_get_next (lrg_block_get_next (hat));
		gfloat    x;
		gfloat    y;
		gfloat    tx;
		gfloat    ty;
		gfloat    th;
		gfloat    sy0;
		gfloat    sy1;

		settle (f->canvas);
		corner (e, say, &x, &y);
		g_assert_true (lrg_block_editor_get_block_bounds (e, lrg_block_get_next (hat), &tx, &ty, NULL, &th));
		g_assert_true (lrg_block_editor_get_block_bounds (e, stop, NULL, &sy0, NULL, NULL));
		move_to (f->canvas, x + 5.0f, y + 5.0f);
		press_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, x + 5.0f, y + 5.0f, LRG_INPUT_MODIFIER_NONE);
		hold_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, (x + tx) * 0.5f, (y + ty) * 0.5f);
		hold_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, tx + 5.0f, ty + th + 5.0f);
		settle (f->canvas);
		g_assert_true (lrg_block_editor_get_block_bounds (e, stop, NULL, &sy1, NULL, NULL));
		g_assert_cmpfloat (sy1, >, sy0 + 20.0f);
		release_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, tx + 5.0f, ty + th + 5.0f);
		settle (f->canvas);
		g_assert_true (lrg_block_get_next (lrg_block_get_next (hat)) == say);
	}

	/* Every preset paints */
	for (i = 0; presets[i] != NULL; i++)
	{
		g_assert_true (lrg_theme_apply_preset (theme, presets[i]));
		settle (f->canvas);
		list = lrg_canvas_record (f->canvas);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, empty);
	}

	/* Zoomed far out the text is culled but the shapes stay */
	lrg_block_editor_set_zoom (e, 0.3f);
	list = lrg_canvas_record (f->canvas);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, empty);
}

/* ==========================================================================
 * GPU pixel check
 * ========================================================================== */

#define GPU_W (480)
#define GPU_H (240)

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
	test_window = grl_window_new (8, 8, "lrg-ui-block-editor-test");
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

/* GPU: workspace background, palette background and a block body in
 * every preset */
static void
test_gpu (void)
{
	const gchar * const  *presets = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas)  canvas = NULL;
	g_autoptr(LrgTheme)   theme = NULL;
	LrgBlockEditor       *e;
	LrgBlock             *move;
	guint                 i;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (canvas), (gfloat) GPU_W, (gfloat) GPU_H);
	lrg_canvas_set_paint_background (canvas, FALSE);
	theme = lrg_theme_new_preset ("midnight");
	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	e = lrg_block_editor_new ();
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (e));
	g_object_unref (e);
	lrg_widget_allocate (LRG_WIDGET (e), 0.0f, 0.0f, (gfloat) GPU_W, (gfloat) GPU_H);
	lrg_block_editor_set_palette_width (e, 120.0f);
	define_palette (e);

	/* The workspace starts at x = 184; the block's body at ws (40, 40),
	 * so the screen point (184 + 40 + 6, 40 + 30) is on it, left of
	 * its text and below its notch */
	move = lrg_block_editor_create_block (e, "move", 40.0f, 40.0f);
	lrg_block_editor_set_show_grid (e, FALSE);

	for (i = 0; presets[i] != NULL; i++)
	{
		GrlColor bg;
		GrlColor body;
		Image    img;

		lrg_theme_apply_preset (theme, presets[i]);
		while (lrg_canvas_update (canvas, DT))
			;
		lrg_paint_color_mix (lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW),
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 0.35f, &bg);
		lrg_block_editor_get_category_color (e, lrg_block_kind_get_category (lrg_block_get_kind (move)), &body);
		img = render_canvas (canvas);
		g_assert_true (pixel_near (&img, 400, 200, &bg, 4));
		g_assert_true (pixel_near (&img, 184 + 46, 70, &body, 18));
		g_assert_true (pixel_near (&img, 120, GPU_H - 6, lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 4));
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

#define ADD(path, func) \
	g_test_add ("/ui/block-editor/" path, Fixture, NULL, fixture_setup, func, fixture_teardown)

	g_test_add_func ("/ui/block-editor/kind-parse", test_kind_parse);
	ADD ("build-api", test_build_api);
	ADD ("snap-statement", test_snap_statement);
	ADD ("snap-mouth", test_snap_mouth);
	ADD ("snap-reporter", test_snap_reporter);
	ADD ("palette-drag", test_palette_drag);
	ADD ("undo-redo", test_undo_redo);
	ADD ("yaml", test_yaml);
	ADD ("view", test_view);
	ADD ("slot-editing", test_slot_editing);
	ADD ("keyboard", test_keyboard);
	ADD ("paint", test_paint);
#undef ADD
	g_test_add_func ("/ui/block-editor/gpu", test_gpu);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
