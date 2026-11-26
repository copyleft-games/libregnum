/* test-input-merge.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgInputMerge (lrg-input-merge.h) over two LrgInputSoftware parts:
 * polling reaches the parts, booleans are "any part", axes take the
 * largest magnitude, the mouse delta adds up, disabled parts are
 * skipped, and the part list itself (add, duplicates, remove, index).
 */

#include <glib.h>
#include <libregnum.h>

typedef struct
{
	LrgInputMerge    *merge;
	LrgInputSoftware *a;
	LrgInputSoftware *b;
} Fixture;

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	(void) data;
	f->merge = lrg_input_merge_new ();
	f->a = lrg_input_software_new ();
	f->b = lrg_input_software_new ();
	lrg_input_merge_add (f->merge, LRG_INPUT (f->a));
	lrg_input_merge_add (f->merge, LRG_INPUT (f->b));
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	(void) data;
	g_clear_object (&f->merge);
	g_clear_object (&f->a);
	g_clear_object (&f->b);
}

/* a key pressed on one part is pressed, down, then released on the merge;
 * the merge's poll is what moves the parts' frame edges */
static void
test_keys_any (Fixture       *f,
               gconstpointer  data)
{
	LrgInput *merge = LRG_INPUT (f->merge);

	(void) data;
	g_assert_false (lrg_input_is_key_down (merge, GRL_KEY_SPACE));

	lrg_input_software_press_key (f->b, GRL_KEY_SPACE);
	lrg_input_poll (merge);
	g_assert_true (lrg_input_is_key_pressed (merge, GRL_KEY_SPACE));
	g_assert_true (lrg_input_is_key_down (merge, GRL_KEY_SPACE));

	lrg_input_poll (merge);
	g_assert_false (lrg_input_is_key_pressed (merge, GRL_KEY_SPACE));
	g_assert_true (lrg_input_is_key_down (merge, GRL_KEY_SPACE));

	lrg_input_software_release_key (f->b, GRL_KEY_SPACE);
	lrg_input_poll (merge);
	g_assert_true (lrg_input_is_key_released (merge, GRL_KEY_SPACE));
	g_assert_false (lrg_input_is_key_down (merge, GRL_KEY_SPACE));
}

/* gamepad and mouse buttons: either part's */
static void
test_buttons_any (Fixture       *f,
                  gconstpointer  data)
{
	LrgInput *merge = LRG_INPUT (f->merge);

	(void) data;
	lrg_input_software_press_gamepad_button (f->a, 0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
	lrg_input_software_press_mouse_button (f->b, GRL_MOUSE_BUTTON_LEFT);
	lrg_input_poll (merge);
	g_assert_true (lrg_input_is_gamepad_button_pressed (merge, 0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
	g_assert_true (lrg_input_is_gamepad_button_down (merge, 0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
	g_assert_false (lrg_input_is_gamepad_button_down (merge, 0, GRL_GAMEPAD_BUTTON_RIGHT_FACE_RIGHT));
	g_assert_true (lrg_input_is_mouse_button_pressed (merge, GRL_MOUSE_BUTTON_LEFT));
	g_assert_true (lrg_input_is_gamepad_available (merge, 0));
}

/* axes: the value furthest from rest wins, whichever part has it */
static void
test_axis_largest (Fixture       *f,
                   gconstpointer  data)
{
	LrgInput *merge = LRG_INPUT (f->merge);

	(void) data;
	lrg_input_software_set_gamepad_axis (f->a, 0, GRL_GAMEPAD_AXIS_LEFT_X, 0.25f);
	lrg_input_software_set_gamepad_axis (f->b, 0, GRL_GAMEPAD_AXIS_LEFT_X, -0.75f);
	lrg_input_software_set_gamepad_axis (f->a, 0, GRL_GAMEPAD_AXIS_LEFT_Y, 0.5f);
	lrg_input_poll (merge);
	g_assert_cmpfloat_with_epsilon (lrg_input_get_gamepad_axis (merge, 0, GRL_GAMEPAD_AXIS_LEFT_X), -0.75f, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_input_get_gamepad_axis (merge, 0, GRL_GAMEPAD_AXIS_LEFT_Y), 0.5f, 1e-6);
	g_assert_cmpfloat_with_epsilon (lrg_input_get_gamepad_axis (merge, 0, GRL_GAMEPAD_AXIS_RIGHT_X), 0.0f, 1e-6);

	/* a tie keeps the earlier part's */
	lrg_input_software_set_gamepad_axis (f->a, 0, GRL_GAMEPAD_AXIS_RIGHT_Y, 0.5f);
	lrg_input_software_set_gamepad_axis (f->b, 0, GRL_GAMEPAD_AXIS_RIGHT_Y, -0.5f);
	lrg_input_poll (merge);
	g_assert_cmpfloat_with_epsilon (lrg_input_get_gamepad_axis (merge, 0, GRL_GAMEPAD_AXIS_RIGHT_Y), 0.5f, 1e-6);
}

/* the mouse: deltas add, the position is the first part's that has one */
static void
test_mouse (Fixture       *f,
            gconstpointer  data)
{
	LrgInput *merge = LRG_INPUT (f->merge);
	gfloat x;
	gfloat y;

	(void) data;
	/* the jump to (30, 40) is motion too; it belongs to the frame before */
	lrg_input_software_move_mouse_to (f->b, 30.0f, 40.0f);
	lrg_input_poll (merge);
	lrg_input_software_move_mouse_by (f->a, 2.0f, 3.0f);
	lrg_input_software_move_mouse_by (f->b, 5.0f, 7.0f);
	lrg_input_poll (merge);

	lrg_input_get_mouse_delta (merge, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 7.0f, 1e-6);
	g_assert_cmpfloat_with_epsilon (y, 10.0f, 1e-6);
	lrg_input_get_mouse_position (merge, &x, &y);
	g_assert_cmpfloat (x != 0.0f || y != 0.0f, ==, TRUE);
}

/* a disabled part says nothing */
static void
test_disabled (Fixture       *f,
               gconstpointer  data)
{
	LrgInput *merge = LRG_INPUT (f->merge);

	(void) data;
	lrg_input_software_press_key (f->a, GRL_KEY_W);
	lrg_input_software_set_gamepad_axis (f->a, 0, GRL_GAMEPAD_AXIS_LEFT_X, 1.0f);
	lrg_input_poll (merge);
	g_assert_true (lrg_input_is_key_down (merge, GRL_KEY_W));

	lrg_input_set_enabled (LRG_INPUT (f->a), FALSE);
	g_assert_false (lrg_input_is_key_down (merge, GRL_KEY_W));
	g_assert_cmpfloat_with_epsilon (lrg_input_get_gamepad_axis (merge, 0, GRL_GAMEPAD_AXIS_LEFT_X), 0.0f, 1e-6);
}

/* the part list: duplicates and the merge itself are ignored */
static void
test_parts (Fixture       *f,
            gconstpointer  data)
{
	g_autoptr(LrgInputMerge) empty = NULL;

	(void) data;
	g_assert_cmpuint (lrg_input_merge_get_n_parts (f->merge), ==, 2);
	lrg_input_merge_add (f->merge, LRG_INPUT (f->a));
	lrg_input_merge_add (f->merge, LRG_INPUT (f->merge));
	g_assert_cmpuint (lrg_input_merge_get_n_parts (f->merge), ==, 2);
	g_assert_true (lrg_input_merge_get_part (f->merge, 0) == LRG_INPUT (f->a));
	g_assert_true (lrg_input_merge_get_part (f->merge, 1) == LRG_INPUT (f->b));
	g_assert_null (lrg_input_merge_get_part (f->merge, 2));

	g_assert_true (lrg_input_merge_remove (f->merge, LRG_INPUT (f->a)));
	g_assert_false (lrg_input_merge_remove (f->merge, LRG_INPUT (f->a)));
	g_assert_cmpuint (lrg_input_merge_get_n_parts (f->merge), ==, 1);

	/* no parts: nothing available, nothing down */
	empty = lrg_input_merge_new ();
	g_assert_false (lrg_input_is_gamepad_available (LRG_INPUT (empty), 0));
	g_assert_false (lrg_input_is_key_down (LRG_INPUT (empty), GRL_KEY_A));
	g_assert_null (lrg_input_get_gamepad_name (LRG_INPUT (empty), 0));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);

#define ADD(path, func) \
	g_test_add (path, Fixture, NULL, fixture_setup, func, fixture_teardown)

	ADD ("/input-merge/keys-any", test_keys_any);
	ADD ("/input-merge/buttons-any", test_buttons_any);
	ADD ("/input-merge/axis-largest", test_axis_largest);
	ADD ("/input-merge/mouse", test_mouse);
	ADD ("/input-merge/disabled", test_disabled);
	ADD ("/input-merge/parts", test_parts);

#undef ADD

	return g_test_run ();
}
