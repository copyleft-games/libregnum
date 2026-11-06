/* test-ui-inspector.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * UI inspector: tree snapshots, picking, highlighting, timings.
 *
 * Every case is headless: the canvas is laid out and recorded into its
 * paint list (lrg_canvas_record) and input is fed as LrgCanvasInput
 * frames through lrg_ui_inspector_filter_input() and
 * lrg_canvas_process_input(), exactly as lrg_ui_inspector_handle_input()
 * does in a real frame loop.
 */

#include <glib.h>
#include <string.h>
#include <json-glib/json-glib.h>
#include <libregnum.h>

/* ==========================================================================
 * Fixture: an 800x600 canvas with a named vbox, two buttons and a label
 * ========================================================================== */

typedef struct
{
	LrgCanvas      *canvas;
	LrgUIInspector *inspector;
	LrgWidget      *vbox;     /* "main", at (20, 20) 300x200 */
	LrgWidget      *ok;       /* LrgButton#ok, class "primary" */
	LrgWidget      *other;    /* unnamed LrgButton */
	LrgWidget      *label;    /* unnamed LrgLabel */
	LrgWidget      *corner;   /* unnamed LrgButton at (500, 400) 120x40 */
} Fixture;

/* add: adds @child to @parent, which keeps the only reference */
static LrgWidget *
add (LrgWidget *parent,
     gpointer   child)
{
	lrg_container_add_child (LRG_CONTAINER (parent), LRG_WIDGET (child));
	g_object_unref (child);
	return LRG_WIDGET (child);
}

static void
fixture_set_up (Fixture       *f,
                gconstpointer  user_data)
{
	f->canvas = lrg_canvas_new ();
	lrg_widget_set_size (LRG_WIDGET (f->canvas), 800.0f, 600.0f);

	f->vbox = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_name (f->vbox, "main");
	lrg_widget_set_position (f->vbox, 20.0f, 20.0f);
	lrg_widget_set_size (f->vbox, 300.0f, 200.0f);
	add (LRG_WIDGET (f->canvas), f->vbox);

	f->ok = LRG_WIDGET (lrg_button_new ("OK"));
	lrg_widget_set_name (f->ok, "ok");
	lrg_widget_add_class (f->ok, "primary");
	lrg_widget_set_margin_all (f->ok, 4.0f);
	add (f->vbox, f->ok);

	f->other = LRG_WIDGET (lrg_button_new ("Other"));
	add (f->vbox, f->other);

	f->label = LRG_WIDGET (lrg_label_new ("Hello"));
	add (f->vbox, f->label);

	f->corner = LRG_WIDGET (lrg_button_new ("Corner"));
	lrg_widget_set_position (f->corner, 500.0f, 400.0f);
	lrg_widget_set_size (f->corner, 120.0f, 40.0f);
	add (LRG_WIDGET (f->canvas), f->corner);

	f->inspector = lrg_ui_inspector_new ();
	lrg_ui_inspector_set_canvas (f->inspector, f->canvas);
	lrg_canvas_record (f->canvas);
}

static void
fixture_tear_down (Fixture       *f,
                   gconstpointer  user_data)
{
	g_clear_object (&f->inspector);
	g_clear_object (&f->canvas);
}

/* center_of: a widget's centre in canvas coordinates */
static void
center_of (LrgWidget *w,
           gfloat    *x,
           gfloat    *y)
{
	*x = lrg_widget_get_world_x (w) + lrg_widget_get_width (w) * 0.5f;
	*y = lrg_widget_get_world_y (w) + lrg_widget_get_height (w) * 0.5f;
}

/* frame_pointer: a frame with the pointer at (@x, @y) and nothing else */
static void
frame_pointer (LrgCanvasInput *input,
               gfloat          x,
               gfloat          y)
{
	lrg_canvas_input_init (input);
	input->pointer_x = x;
	input->pointer_y = y;
	input->delta_time = 1.0f / 60.0f;
}

/* feed: one frame through the inspector filter, then the canvas */
static void
feed (Fixture        *f,
      LrgCanvasInput *input)
{
	lrg_ui_inspector_filter_input (f->inspector, input);
	lrg_canvas_process_input (f->canvas, input);
}

/* click_at: move, press, release at (@x, @y) */
static void
click_at (Fixture *f,
          gfloat   x,
          gfloat   y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	feed (f, &input);

	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	feed (f, &input);

	frame_pointer (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	feed (f, &input);
}

static void
on_count (gpointer instance,
          gpointer user_data)
{
	(*(gint *) user_data)++;
}

static void
on_picked (LrgUIInspector *inspector,
           LrgWidget      *widget,
           gpointer        user_data)
{
	*(LrgWidget **) user_data = widget;
}

/* ==========================================================================
 * Snapshots
 * ========================================================================== */

static void
test_snapshot_json (Fixture       *f,
                    gconstpointer  user_data)
{
	g_autofree gchar    *json = NULL;
	g_autoptr(JsonParser) parser = json_parser_new ();
	g_autoptr(GError)    error = NULL;
	JsonObject          *top;
	JsonObject          *root;
	JsonObject          *vbox;
	JsonObject          *ok;
	JsonObject          *rect;
	JsonObject          *look;
	JsonArray           *children;

	json = lrg_ui_inspector_snapshot_json (f->inspector, FALSE);
	g_assert_true (json_parser_load_from_data (parser, json, -1, &error));
	g_assert_no_error (error);
	top = json_node_get_object (json_parser_get_root (parser));

	/* Top level */
	g_assert_cmpfloat (json_object_get_double_member (json_object_get_object_member (top, "canvas"),
	                                                  "width"), ==, 800.0);
	g_assert_true (json_object_has_member (top, "popups"));
	g_assert_true (json_object_has_member (top, "overlays"));
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (top, "overlays")), ==, 0);
	g_assert_true (json_object_get_null_member (top, "selected"));

	/* The canvas root and its two children */
	root = json_object_get_object_member (top, "root");
	g_assert_cmpstr (json_object_get_string_member (root, "type"), ==, "LrgCanvas");
	g_assert_cmpstr (json_object_get_string_member (root, "path"), ==, "");
	g_assert_cmpint (json_object_get_int_member (root, "n_children"), ==, 2);
	children = json_object_get_array_member (root, "children");
	vbox = json_array_get_object_element (children, 0);
	g_assert_cmpstr (json_object_get_string_member (vbox, "type"), ==, "LrgVBox");
	g_assert_cmpstr (json_object_get_string_member (vbox, "name"), ==, "main");
	g_assert_cmpstr (json_object_get_string_member (vbox, "path"), ==, "LrgVBox#main");
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (children, 1), "path"),
	                 ==, "LrgButton[0]");

	/* A full node */
	ok = json_array_get_object_element (json_object_get_array_member (vbox, "children"), 0);
	g_assert_cmpstr (json_object_get_string_member (ok, "path"), ==, "LrgVBox#main/LrgButton#ok");
	g_assert_cmpstr (json_array_get_string_element (json_object_get_array_member (ok, "classes"), 0),
	                 ==, "primary");
	g_assert_true (JSON_NODE_HOLDS_ARRAY (json_object_get_member (ok, "state")));
	rect = json_object_get_object_member (ok, "rect");
	g_assert_cmpfloat (json_object_get_double_member (rect, "x"), ==, lrg_widget_get_world_x (f->ok));
	g_assert_cmpfloat (json_object_get_double_member (rect, "width"), >, 0.0);
	g_assert_cmpfloat (json_object_get_double_member (json_object_get_object_member (ok, "margin"), "left"),
	                   ==, 4.0);
	g_assert_cmpstr (json_object_get_string_member (ok, "halign"), ==, "fill");
	g_assert_true (json_object_get_boolean_member (ok, "visible"));
	g_assert_true (json_object_get_boolean_member (ok, "focusable"));
	g_assert_cmpint (json_object_get_int_member (ok, "n_children"), ==, 0);
	look = json_object_get_object_member (ok, "look");
	g_assert_cmpstr (json_object_get_string_member (look, "part"), ==, "box");
	g_assert_true (g_str_has_prefix (json_object_get_string_member (look, "background"), "#"));
	g_assert_cmpuint (strlen (json_object_get_string_member (look, "background")), ==, 9);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (look, "radii")), ==, 4);
}

static void
test_snapshot_variant (Fixture       *f,
                       gconstpointer  user_data)
{
	g_autoptr(GVariant) nodes = lrg_ui_inspector_snapshot (f->inspector);
	g_autoptr(GVariant) second = NULL;
	const gchar        *path = NULL;
	const gchar        *type = NULL;
	guint32             depth = 0;

	g_assert_true (g_variant_is_of_type (nodes, G_VARIANT_TYPE ("aa{sv}")));
	/* canvas, vbox, ok, other, label, corner */
	g_assert_cmpuint (g_variant_n_children (nodes), ==, 6);

	second = g_variant_get_child_value (nodes, 2);
	g_assert_true (g_variant_lookup (second, "path", "&s", &path));
	g_assert_true (g_variant_lookup (second, "type", "&s", &type));
	g_assert_true (g_variant_lookup (second, "depth", "u", &depth));
	g_assert_cmpstr (path, ==, "LrgVBox#main/LrgButton#ok");
	g_assert_cmpstr (type, ==, "LrgButton");
	g_assert_cmpuint (depth, ==, 2);
}

/* ==========================================================================
 * Paths
 * ========================================================================== */

static void
test_path_round_trip (Fixture       *f,
                      gconstpointer  user_data)
{
	g_autoptr(GVariant) nodes = lrg_ui_inspector_snapshot (f->inspector);
	g_autoptr(LrgButton) twin = NULL;
	gsize               i;

	/* Every node: path -> widget -> the same path */
	for (i = 0; i < g_variant_n_children (nodes); i++)
	{
		g_autoptr(GVariant) node = g_variant_get_child_value (nodes, i);
		g_autofree gchar   *again = NULL;
		const gchar        *path = NULL;
		LrgWidget          *widget;

		g_assert_true (g_variant_lookup (node, "path", "&s", &path));
		widget = lrg_ui_inspector_lookup (f->inspector, path);
		g_assert_nonnull (widget);
		again = lrg_ui_inspector_get_path (f->inspector, widget);
		g_assert_cmpstr (again, ==, path);
	}

	/* Known paths and the short forms */
	g_assert_true (lrg_ui_inspector_lookup (f->inspector, "") == LRG_WIDGET (f->canvas));
	g_assert_true (lrg_ui_inspector_lookup (f->inspector, "/") == LRG_WIDGET (f->canvas));
	g_assert_true (lrg_ui_inspector_lookup (f->inspector, "LrgVBox#main/LrgButton#ok") == f->ok);
	g_assert_true (lrg_ui_inspector_lookup (f->inspector, "#main/#ok") == f->ok);
	g_assert_true (lrg_ui_inspector_lookup (f->inspector, "LrgVBox[0]/LrgButton[1]") == f->other);
	g_assert_true (lrg_ui_inspector_lookup (f->inspector, "LrgVBox#main/LrgLabel[0]") == f->label);
	g_assert_true (lrg_ui_inspector_lookup (f->inspector, "LrgButton[0]") == f->corner);

	/* Misses */
	g_assert_null (lrg_ui_inspector_lookup (f->inspector, "LrgVBox#main/LrgButton[7]"));
	g_assert_null (lrg_ui_inspector_lookup (f->inspector, "LrgHBox[0]"));
	g_assert_null (lrg_ui_inspector_lookup (f->inspector, "LrgVBox[x]"));
	g_assert_null (lrg_ui_inspector_lookup (f->inspector, ":popup[0]"));

	/* Same name twice: the second gets an index */
	twin = lrg_button_new ("Twin");
	lrg_widget_set_name (LRG_WIDGET (twin), "ok");
	lrg_container_add_child (LRG_CONTAINER (f->vbox), LRG_WIDGET (twin));
	{
		g_autofree gchar *path = lrg_ui_inspector_get_path (f->inspector, LRG_WIDGET (twin));

		g_assert_cmpstr (path, ==, "LrgVBox#main/LrgButton#ok[1]");
		g_assert_true (lrg_ui_inspector_lookup (f->inspector, path) == LRG_WIDGET (twin));
	}

	/* A widget outside the canvas has no path */
	{
		g_autoptr(LrgButton) loose = lrg_button_new ("Loose");

		g_assert_null (lrg_ui_inspector_get_path (f->inspector, LRG_WIDGET (loose)));
	}
}

/* ==========================================================================
 * Picking
 * ========================================================================== */

static void
test_pick_layers (Fixture       *f,
                  gconstpointer  user_data)
{
	g_autoptr(LrgPanel) veil_layer = NULL;
	g_autoptr(LrgPanel) badge = NULL;
	g_autoptr(LrgPanel) popup = NULL;
	g_autoptr(LrgButton) inside = NULL;
	g_autofree gchar   *path = NULL;
	gfloat              x;
	gfloat              y;

	/* Plain tree */
	center_of (f->corner, &x, &y);
	g_assert_true (lrg_ui_inspector_pick (f->inspector, x, y) == f->corner);
	g_assert_null (lrg_ui_inspector_pick (f->inspector, 790.0f, 10.0f));

	/* A full-canvas pointer-transparent overlay lets the pick through */
	veil_layer = lrg_panel_new ();
	lrg_widget_set_size (LRG_WIDGET (veil_layer), 800.0f, 600.0f);
	lrg_widget_set_pointer_transparent (LRG_WIDGET (veil_layer), TRUE);
	lrg_canvas_add_overlay (f->canvas, LRG_WIDGET (veil_layer));
	lrg_canvas_record (f->canvas);
	g_assert_true (lrg_ui_inspector_pick (f->inspector, x, y) == f->corner);

	/* An opaque overlay over the button wins */
	badge = lrg_panel_new ();
	lrg_widget_set_position (LRG_WIDGET (badge), 490.0f, 390.0f);
	lrg_widget_set_size (LRG_WIDGET (badge), 60.0f, 60.0f);
	lrg_canvas_add_overlay (f->canvas, LRG_WIDGET (badge));
	lrg_canvas_record (f->canvas);
	g_assert_true (lrg_ui_inspector_pick (f->inspector, 500.0f, 410.0f) == LRG_WIDGET (badge));
	path = lrg_ui_inspector_get_path (f->inspector, LRG_WIDGET (badge));
	g_assert_cmpstr (path, ==, ":overlay[1]");
	g_assert_true (lrg_ui_inspector_lookup (f->inspector, ":overlay[1]") == LRG_WIDGET (badge));
	g_clear_pointer (&path, g_free);

	/* A popup is above the tree; its child is what is picked */
	popup = lrg_panel_new ();
	lrg_widget_set_size (LRG_WIDGET (popup), 200.0f, 100.0f);
	inside = lrg_button_new ("Inside");
	lrg_widget_set_position (LRG_WIDGET (inside), 10.0f, 10.0f);
	lrg_widget_set_size (LRG_WIDGET (inside), 100.0f, 30.0f);
	lrg_container_add_child (LRG_CONTAINER (popup), LRG_WIDGET (inside));
	lrg_canvas_show_popup (f->canvas, LRG_WIDGET (popup), NULL, LRG_POPUP_PLACEMENT_CENTER, FALSE);
	lrg_canvas_record (f->canvas);
	center_of (LRG_WIDGET (inside), &x, &y);
	g_assert_true (lrg_ui_inspector_pick (f->inspector, x, y) == LRG_WIDGET (inside));
	path = lrg_ui_inspector_get_path (f->inspector, LRG_WIDGET (inside));
	g_assert_cmpstr (path, ==, ":popup[0]/LrgButton[0]");
	g_assert_true (lrg_ui_inspector_lookup (f->inspector, path) == LRG_WIDGET (inside));
	g_clear_pointer (&path, g_free);

	/* Pick agrees with the canvas hit-test everywhere we look */
	g_assert_true (lrg_ui_inspector_pick (f->inspector, x, y) ==
	               lrg_canvas_widget_at_point (f->canvas, x, y));

	/* A modal popup's veil hides the tree below it */
	lrg_canvas_dismiss_popup (f->canvas, LRG_WIDGET (popup));
	lrg_canvas_show_popup (f->canvas, LRG_WIDGET (popup), NULL, LRG_POPUP_PLACEMENT_CENTER, TRUE);
	lrg_canvas_record (f->canvas);
	center_of (f->ok, &x, &y);
	g_assert_null (lrg_ui_inspector_pick (f->inspector, x, y));

	/* Snapshots list the app's overlays but not the inspector's own */
	{
		g_autofree gchar     *json = lrg_ui_inspector_snapshot_json (f->inspector, FALSE);
		g_autoptr(JsonParser) parser = json_parser_new ();
		JsonObject           *top;

		g_assert_true (json_parser_load_from_data (parser, json, -1, NULL));
		top = json_node_get_object (json_parser_get_root (parser));
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (top, "overlays")), ==, 2);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (top, "popups")), ==, 1);
	}
	g_assert_cmpuint (lrg_canvas_get_n_overlays (f->canvas), ==, 3);
}

static void
test_pick_mode_swallows_clicks (Fixture       *f,
                                gconstpointer  user_data)
{
	LrgCanvasInput input;
	LrgWidget     *picked = NULL;
	gint           clicks = 0;
	gfloat         x;
	gfloat         y;

	g_signal_connect (f->corner, "clicked", G_CALLBACK (on_count), &clicks);
	g_signal_connect (f->inspector, "picked", G_CALLBACK (on_picked), &picked);
	center_of (f->corner, &x, &y);

	/* Pick mode: the click selects and never reaches the button */
	lrg_ui_inspector_set_pick_mode (f->inspector, TRUE);
	click_at (f, x, y);
	g_assert_cmpint (clicks, ==, 0);
	g_assert_true (picked == f->corner);
	g_assert_true (lrg_ui_inspector_get_selected (f->inspector) == f->corner);
	g_assert_true (lrg_ui_inspector_get_hovered (f->inspector) == f->corner);
	g_assert_null (lrg_canvas_get_focused_widget (f->canvas));
	g_assert_false (lrg_widget_get_state_flags (f->corner) & LRG_WIDGET_STATE_PRESSED);

	/* Escape leaves pick mode and is consumed */
	frame_pointer (&input, x, y);
	lrg_canvas_input_add_key (&input, GRL_KEY_ESCAPE);
	feed (f, &input);
	g_assert_cmpuint (input.n_keys, ==, 0);
	g_assert_false (lrg_ui_inspector_get_pick_mode (f->inspector));
	g_assert_null (lrg_ui_inspector_get_hovered (f->inspector));

	/* Outside pick mode clicks work again */
	click_at (f, x, y);
	g_assert_cmpint (clicks, ==, 1);

	/* A press made before pick mode still gets its release */
	frame_pointer (&input, x, y);
	input.buttons_down = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	input.buttons_pressed = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	feed (f, &input);
	lrg_ui_inspector_set_pick_mode (f->inspector, TRUE);
	frame_pointer (&input, x, y);
	input.buttons_released = LRG_CANVAS_BUTTON_MASK (GRL_MOUSE_BUTTON_LEFT);
	feed (f, &input);
	g_assert_cmpuint (input.buttons_released, !=, 0);
	g_assert_false (lrg_widget_get_state_flags (f->corner) & LRG_WIDGET_STATE_PRESSED);
}

/* ==========================================================================
 * Highlight
 * ========================================================================== */

static void
test_highlight_records_instances (Fixture       *f,
                                  gconstpointer  user_data)
{
	g_autoptr(LrgPaintList) own = lrg_paint_list_new ();
	g_autoptr(GError)       error = NULL;
	LrgUIInspectorTimings   timings;
	guint                   before;
	guint                   after;

	before = lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas));

	g_assert_true (lrg_ui_inspector_highlight (f->inspector, "LrgVBox#main/LrgButton#ok", &error));
	g_assert_no_error (error);
	g_assert_true (lrg_ui_inspector_get_selected (f->inspector) == f->ok);
	after = lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas));

	/* margin (4 strips) + fill + border + label background, at least */
	g_assert_cmpuint (after, >=, before + 7);

	/* The timings report the app's own instances */
	lrg_ui_inspector_get_timings (f->inspector, &timings);
	g_assert_cmpuint (timings.n_instances, ==, before);

	/* The highlight can also go into a list of your own */
	lrg_ui_inspector_paint_highlight (f->inspector, own);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (own), >=, 7);

	/* Hidden highlight: nothing extra */
	lrg_ui_inspector_set_highlight_visible (f->inspector, FALSE);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas)), ==, before);
	lrg_ui_inspector_set_highlight_visible (f->inspector, TRUE);

	/* Bad path: error, selection unchanged; NULL clears */
	g_assert_false (lrg_ui_inspector_highlight (f->inspector, "LrgNope[0]", &error));
	g_assert_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_NOT_FOUND);
	g_assert_true (lrg_ui_inspector_get_selected (f->inspector) == f->ok);
	g_assert_true (lrg_ui_inspector_highlight (f->inspector, NULL, NULL));
	g_assert_null (lrg_ui_inspector_get_selected (f->inspector));
	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (f->canvas)), ==, before);
}

/* ==========================================================================
 * Timings
 * ========================================================================== */

static void
test_timings (void)
{
	g_autoptr(LrgCanvas)      canvas = lrg_canvas_new ();
	g_autoptr(LrgUIInspector) inspector = lrg_ui_inspector_new ();
	LrgUIInspectorTimings     timings;
	LrgCanvasInput            input;
	LrgWidget                *box;
	guint                     i;

	lrg_widget_set_size (LRG_WIDGET (canvas), 800.0f, 600.0f);
	box = LRG_WIDGET (lrg_vbox_new ());
	lrg_widget_set_size (box, 800.0f, 600.0f);
	add (LRG_WIDGET (canvas), box);
	for (i = 0; i < 400; i++)
	{
		g_autofree gchar *text = g_strdup_printf ("Button %u", i);

		add (box, lrg_button_new (text));
	}

	/* Nothing measured before attaching or before a frame */
	lrg_ui_inspector_get_timings (inspector, &timings);
	g_assert_cmpint (timings.layout_us, ==, 0);
	lrg_ui_inspector_set_canvas (inspector, canvas);
	lrg_ui_inspector_get_timings (inspector, &timings);
	g_assert_cmpuint (timings.frames, ==, 0);

	/* One frame: input (steps every widget), then record */
	lrg_canvas_input_init (&input);
	input.pointer_x = 10.0f;
	input.pointer_y = 10.0f;
	input.delta_time = 1.0f / 60.0f;
	lrg_ui_inspector_filter_input (inspector, &input);
	lrg_canvas_process_input (canvas, &input);
	lrg_canvas_record (canvas);

	lrg_ui_inspector_get_timings (inspector, &timings);
	g_assert_cmpint (timings.step_us, >, 0);
	g_assert_cmpint (timings.layout_us, >, 0);
	g_assert_cmpint (timings.record_us, >, 0);
	g_assert_cmpuint (timings.n_instances, >, 0);
	g_assert_cmpuint (timings.n_commands, >, 0);
	g_assert_cmpuint (timings.frames, ==, 1);

	/* The boxed copy */
	{
		g_autoptr(LrgUIInspectorTimings) copy = lrg_ui_inspector_timings_copy (&timings);

		g_assert_cmpint (copy->record_us, ==, timings.record_us);
		g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_UI_INSPECTOR_TIMINGS));
	}
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

static void
test_property_by_name (Fixture       *f,
                       gconstpointer  user_data)
{
	const gchar      *path = "LrgVBox#main/LrgButton#ok";
	g_autoptr(GError) error = NULL;
	g_autofree gchar *described = NULL;
	GValue            value = G_VALUE_INIT;
	GValue            number = G_VALUE_INIT;

	/* Text, boolean, enum, float, colour */
	g_assert_true (lrg_ui_inspector_set_property_from_string (f->inspector, path, "text", "Apply", &error));
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_button_get_text (LRG_BUTTON (f->ok)), ==, "Apply");
	g_assert_true (lrg_ui_inspector_set_property_from_string (f->inspector, path, "halign", "center", NULL));
	g_assert_cmpint (lrg_widget_get_halign (f->ok), ==, LRG_ALIGN_CENTER);
	g_assert_true (lrg_ui_inspector_set_property_from_string (f->inspector, path, "opacity", "0.5", NULL));
	g_assert_cmpfloat_with_epsilon (lrg_widget_get_opacity (f->ok), 0.5f, 1e-6);
	g_assert_true (lrg_ui_inspector_set_property_from_string (f->inspector, path, "enabled", "false", NULL));
	g_assert_false (lrg_widget_get_enabled (f->ok));
	g_assert_true (lrg_ui_inspector_set_property_from_string (f->inspector, path, "normal-color",
	                                                          "#ff000080", NULL));

	/* A GValue of another type is converted */
	g_value_init (&number, G_TYPE_INT);
	g_value_set_int (&number, 1);
	g_assert_true (lrg_ui_inspector_set_property (f->inspector, path, "opacity", &number, &error));
	g_assert_no_error (error);
	g_assert_cmpfloat (lrg_widget_get_opacity (f->ok), ==, 1.0f);
	g_value_unset (&number);

	/* Read back */
	g_assert_true (lrg_ui_inspector_get_property (f->inspector, path, "text", &value, &error));
	g_assert_cmpstr (g_value_get_string (&value), ==, "Apply");
	g_value_unset (&value);

	/* The description carries the properties */
	described = lrg_ui_inspector_describe_widget (f->inspector, f->ok, FALSE);
	g_assert_nonnull (strstr (described, "\"text\":\"Apply\""));
	g_assert_nonnull (strstr (described, "\"normal-color\":\"#ff000080\""));
	g_assert_nonnull (strstr (described, "\"halign\":\"center\""));

	/* Errors */
	g_assert_false (lrg_ui_inspector_set_property_from_string (f->inspector, path, "nope", "1", &error));
	g_assert_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_INVALID_PROPERTY);
	g_clear_error (&error);
	g_assert_false (lrg_ui_inspector_set_property_from_string (f->inspector, path, "opacity", "abc", &error));
	g_assert_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_INVALID_VALUE);
	g_clear_error (&error);
	g_assert_false (lrg_ui_inspector_set_property_from_string (f->inspector, path, "opacity", "5", &error));
	g_assert_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_INVALID_VALUE);
	g_clear_error (&error);
	g_assert_false (lrg_ui_inspector_set_property_from_string (f->inspector, path, "is-hovered", "true",
	                                                           &error));
	g_assert_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_INVALID_PROPERTY);
	g_clear_error (&error);
	g_assert_false (lrg_ui_inspector_set_property_from_string (f->inspector, "Nope[0]", "text", "x", &error));
	g_assert_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_NOT_FOUND);
}

/* ==========================================================================
 * Lifetime
 * ========================================================================== */

static void
test_weak_ref_canvas_freed (void)
{
	g_autoptr(LrgUIInspector) inspector = lrg_ui_inspector_new ();
	g_autoptr(GError)         error = NULL;
	g_autofree gchar         *json = NULL;
	LrgUIInspectorTimings     timings;
	LrgCanvasInput            input;
	LrgCanvas                *canvas = lrg_canvas_new ();
	LrgWidget                *button = LRG_WIDGET (lrg_button_new ("B"));

	lrg_widget_set_size (LRG_WIDGET (canvas), 400.0f, 300.0f);
	lrg_widget_set_size (button, 100.0f, 30.0f);
	add (LRG_WIDGET (canvas), button);
	lrg_ui_inspector_set_canvas (inspector, canvas);
	lrg_ui_inspector_set_selected (inspector, button);
	lrg_ui_inspector_set_pick_mode (inspector, TRUE);
	lrg_canvas_record (canvas);

	/* The canvas goes away: everything detaches */
	g_object_unref (canvas);
	g_assert_null (lrg_ui_inspector_get_canvas (inspector));
	g_assert_null (lrg_ui_inspector_get_selected (inspector));
	g_assert_null (lrg_ui_inspector_lookup (inspector, ""));
	g_assert_null (lrg_ui_inspector_pick (inspector, 10.0f, 10.0f));
	json = lrg_ui_inspector_snapshot_json (inspector, FALSE);
	g_assert_nonnull (strstr (json, "error"));
	lrg_ui_inspector_get_timings (inspector, &timings);
	g_assert_cmpuint (timings.frames, ==, 0);
	g_assert_false (lrg_ui_inspector_highlight (inspector, "LrgButton[0]", &error));
	g_assert_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_NO_CANVAS);
	lrg_canvas_input_init (&input);
	g_assert_false (lrg_ui_inspector_filter_input (inspector, &input));
	{
		g_autoptr(GVariant) nodes = lrg_ui_inspector_snapshot (inspector);

		g_assert_cmpuint (g_variant_n_children (nodes), ==, 0);
	}
}

static void
test_weak_ref_inspector_freed (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgUIInspector      *inspector = lrg_ui_inspector_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), 400.0f, 300.0f);
	lrg_ui_inspector_set_canvas (inspector, canvas);
	g_assert_cmpuint (lrg_canvas_get_n_overlays (canvas), ==, 1);

	/* Freeing the inspector removes its overlay; the canvas keeps working */
	g_object_unref (inspector);
	g_assert_cmpuint (lrg_canvas_get_n_overlays (canvas), ==, 0);
	lrg_canvas_record (canvas);

	/* Reattaching and switching canvases moves the overlay */
	inspector = lrg_ui_inspector_new ();
	lrg_ui_inspector_set_canvas (inspector, canvas);
	g_assert_cmpuint (lrg_canvas_get_n_overlays (canvas), ==, 1);
	lrg_ui_inspector_set_canvas (inspector, NULL);
	g_assert_cmpuint (lrg_canvas_get_n_overlays (canvas), ==, 0);
	g_object_unref (inspector);
}

static void
test_default_registration (void)
{
	g_autoptr(LrgCanvas) canvas = lrg_canvas_new ();
	LrgUIInspector      *inspector = lrg_ui_inspector_get_default ();

	g_assert_true (inspector == lrg_ui_inspector_get_default ());
	lrg_ui_inspector_register_canvas (canvas);
	g_assert_true (lrg_ui_inspector_get_canvas (inspector) == canvas);
	lrg_ui_inspector_register_canvas (NULL);
	g_assert_null (lrg_ui_inspector_get_canvas (inspector));
}

/* ==========================================================================
 * MCP tools (only with MCP=1)
 * ========================================================================== */

#ifdef LRG_ENABLE_MCP

/* mcp_call: calls a debug tool and parses its JSON text (NULL on error) */
static JsonObject *
mcp_call (LrgMcpToolProvider  *provider,
          const gchar         *tool,
          const gchar         *arguments,
          GError             **error)
{
	g_autoptr(JsonParser)    args = json_parser_new ();
	g_autoptr(JsonParser)    output = json_parser_new ();
	g_autoptr(McpToolResult) result = NULL;
	JsonObject              *content;

	g_assert_true (json_parser_load_from_data (args, arguments, -1, NULL));
	result = lrg_mcp_tool_provider_call_tool (provider, tool,
	                                          json_node_get_object (json_parser_get_root (args)), error);
	if (result == NULL)
		return NULL;
	content = json_array_get_object_element (mcp_tool_result_get_content (result), 0);
	g_assert_true (json_parser_load_from_data (output, json_object_get_string_member (content, "text"),
	                                           -1, NULL));
	return json_object_ref (json_node_get_object (json_parser_get_root (output)));
}

static void
test_mcp_tools (Fixture       *f,
                gconstpointer  user_data)
{
	g_autoptr(LrgMcpDebugTools) tools = lrg_mcp_debug_tools_new ();
	LrgMcpToolProvider         *provider = LRG_MCP_TOOL_PROVIDER (tools);
	g_autoptr(GError)           error = NULL;
	g_autofree gchar           *args = NULL;
	JsonObject                 *o;
	gfloat                      x;
	gfloat                      y;

	/* Nothing registered yet */
	lrg_ui_inspector_register_canvas (NULL);
	g_assert_null (mcp_call (provider, "lrg_ui_tree", "{}", &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);

	lrg_ui_inspector_register_canvas (f->canvas);

	/* lrg_ui_tree */
	o = mcp_call (provider, "lrg_ui_tree", "{}", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (o, "root"), "type"),
	                 ==, "LrgCanvas");
	json_object_unref (o);

	/* lrg_ui_pick + select */
	center_of (f->corner, &x, &y);
	args = g_strdup_printf ("{\"x\": %.1f, \"y\": %.1f, \"select\": true}", x, y);
	o = mcp_call (provider, "lrg_ui_pick", args, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (json_object_get_string_member (o, "path"), ==, "LrgButton[0]");
	g_assert_true (lrg_ui_inspector_get_selected (lrg_ui_inspector_get_default ()) == f->corner);
	json_object_unref (o);
	o = mcp_call (provider, "lrg_ui_pick", "{\"x\": 790, \"y\": 5}", &error);
	g_assert_true (json_object_get_null_member (o, "path"));
	json_object_unref (o);

	/* lrg_ui_get_widget */
	o = mcp_call (provider, "lrg_ui_get_widget", "{\"path\": \"#main/#ok\"}", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (o, "properties"), "text"),
	                 ==, "OK");
	json_object_unref (o);
	g_assert_null (mcp_call (provider, "lrg_ui_get_widget", "{\"path\": \"Nope[0]\"}", &error));
	g_clear_error (&error);

	/* lrg_ui_set_property: text, number and enum nick */
	o = mcp_call (provider, "lrg_ui_set_property",
	              "{\"path\": \"#main/#ok\", \"property\": \"text\", \"value\": \"Go\"}", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_button_get_text (LRG_BUTTON (f->ok)), ==, "Go");
	json_object_unref (o);
	o = mcp_call (provider, "lrg_ui_set_property",
	              "{\"path\": \"#main/#ok\", \"property\": \"opacity\", \"value\": 0.25}", &error);
	g_assert_no_error (error);
	g_assert_cmpfloat_with_epsilon (lrg_widget_get_opacity (f->ok), 0.25f, 1e-6);
	json_object_unref (o);
	g_assert_null (mcp_call (provider, "lrg_ui_set_property",
	                         "{\"path\": \"#main/#ok\", \"property\": \"opacity\", \"value\": 9}", &error));
	g_assert_error (error, LRG_UI_INSPECTOR_ERROR, LRG_UI_INSPECTOR_ERROR_INVALID_VALUE);
	g_clear_error (&error);

	/* lrg_ui_highlight and pick mode */
	o = mcp_call (provider, "lrg_ui_highlight", "{\"path\": \"#main/#ok\", \"pick_mode\": true}", &error);
	g_assert_no_error (error);
	g_assert_cmpstr (json_object_get_string_member (o, "selected"), ==, "LrgVBox#main/LrgButton#ok");
	g_assert_true (json_object_get_boolean_member (o, "pick_mode"));
	json_object_unref (o);
	o = mcp_call (provider, "lrg_ui_highlight", "{\"pick_mode\": false}", &error);
	g_assert_true (json_object_get_null_member (o, "selected"));
	json_object_unref (o);

	/* lrg_ui_timings */
	lrg_canvas_record (f->canvas);
	o = mcp_call (provider, "lrg_ui_timings", "{}", &error);
	g_assert_no_error (error);
	g_assert_cmpint (json_object_get_int_member (o, "frames"), >=, 1);
	g_assert_cmpint (json_object_get_int_member (o, "n_instances"), >, 0);
	json_object_unref (o);

	lrg_ui_inspector_register_canvas (NULL);
}

#endif /* LRG_ENABLE_MCP */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/ui-inspector/snapshot/json", Fixture, NULL,
	            fixture_set_up, test_snapshot_json, fixture_tear_down);
	g_test_add ("/ui-inspector/snapshot/variant", Fixture, NULL,
	            fixture_set_up, test_snapshot_variant, fixture_tear_down);
	g_test_add ("/ui-inspector/path/round-trip", Fixture, NULL,
	            fixture_set_up, test_path_round_trip, fixture_tear_down);
	g_test_add ("/ui-inspector/pick/layers", Fixture, NULL,
	            fixture_set_up, test_pick_layers, fixture_tear_down);
	g_test_add ("/ui-inspector/pick/mode-swallows-clicks", Fixture, NULL,
	            fixture_set_up, test_pick_mode_swallows_clicks, fixture_tear_down);
	g_test_add ("/ui-inspector/highlight/records-instances", Fixture, NULL,
	            fixture_set_up, test_highlight_records_instances, fixture_tear_down);
	g_test_add_func ("/ui-inspector/timings", test_timings);
	g_test_add ("/ui-inspector/property/by-name", Fixture, NULL,
	            fixture_set_up, test_property_by_name, fixture_tear_down);
	g_test_add_func ("/ui-inspector/lifetime/canvas-freed", test_weak_ref_canvas_freed);
	g_test_add_func ("/ui-inspector/lifetime/inspector-freed", test_weak_ref_inspector_freed);
	g_test_add_func ("/ui-inspector/default", test_default_registration);
#ifdef LRG_ENABLE_MCP
	g_test_add ("/ui-inspector/mcp/tools", Fixture, NULL,
	            fixture_set_up, test_mcp_tools, fixture_tear_down);
#endif

	return g_test_run ();
}
