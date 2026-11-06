/* test-ui-node-graph.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNodeGraph: model, connection rules, view maths, hit testing,
 * pointer and keyboard interaction, culling and YAML round trips.
 *
 * Headless: the graph sits on a canvas, is driven through
 * lrg_canvas_process_input() and stepped at 60 Hz with
 * lrg_canvas_update(), so every animation is deterministic. One GPU
 * case renders a node in every theme preset and probes pixels; it
 * skips without a display.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

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
drag_button (LrgCanvas      *canvas,
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

/* drag: a full left-button drag from (x0, y0) to (x1, y1) in @steps moves */
static void
drag (LrgCanvas         *canvas,
      gfloat             x0,
      gfloat             y0,
      gfloat             x1,
      gfloat             y1,
      guint              steps,
      LrgInputModifiers  mods)
{
	guint i;

	move_to (canvas, x0, y0);
	press_button (canvas, GRL_MOUSE_BUTTON_LEFT, x0, y0, mods);
	for (i = 1; i <= steps; i++)
	{
		gfloat t = (gfloat) i / (gfloat) steps;

		drag_button (canvas, GRL_MOUSE_BUTTON_LEFT, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
	}
	release_button (canvas, GRL_MOUSE_BUTTON_LEFT, x1, y1);
}

static void
click_at (LrgCanvas         *canvas,
          gfloat             x,
          gfloat             y,
          LrgInputModifiers  mods)
{
	move_to (canvas, x, y);
	press_button (canvas, GRL_MOUSE_BUTTON_LEFT, x, y, mods);
	release_button (canvas, GRL_MOUSE_BUTTON_LEFT, x, y);
}

static void
key (LrgCanvas         *canvas,
     GrlKey             k,
     LrgInputModifiers  mods)
{
	LrgCanvasInput input;

	frame_pointer (&input, 799.0f, 1.0f);
	input.modifiers = mods;
	lrg_canvas_input_add_key (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

static void
wheel (LrgCanvas *canvas,
       gfloat     x,
       gfloat     y,
       gfloat     amount)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	input.wheel_y = amount;
	lrg_canvas_process_input (canvas, &input);
}

/* settle: run frames until nothing animates (at most @max); returns the count */
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
count_cb (gpointer user_data)
{
	(*(guint *) user_data)++;
}

/* ==========================================================================
 * Fixture
 *
 * A graph filling the 800 x 600 canvas, view centred on graph (200, 100)
 * at zoom 1, so graph (x, y) shows at canvas (x + 200, y + 200):
 *
 *   a  (0, 0)     out: float, vec (outputs)
 *   b  (300, 0)   in: float (input), extra: any (input)
 *   c  (300, 200) in: string (input)
 * ========================================================================== */

typedef struct
{
	LrgCanvas    *canvas;
	LrgNodeGraph *graph;
	LrgGraphNode *a;
	LrgGraphNode *b;
	LrgGraphNode *c;
	LrgGraphPort *a_out;
	LrgGraphPort *a_vec;
	LrgGraphPort *b_in;
	LrgGraphPort *b_extra;
	LrgGraphPort *c_in;
} Fixture;

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	f->canvas = canvas_new ();
	f->graph = lrg_node_graph_new ();
	lrg_container_add_child (LRG_CONTAINER (f->canvas), LRG_WIDGET (f->graph));
	g_object_unref (f->graph);
	lrg_widget_allocate (LRG_WIDGET (f->graph), 0.0f, 0.0f, 800.0f, 600.0f);

	f->a = lrg_node_graph_add_node (f->graph, "a", "Source", 0.0f, 0.0f);
	f->a_out = lrg_graph_node_add_port (f->a, LRG_GRAPH_PORT_OUTPUT, "out", "float");
	f->a_vec = lrg_graph_node_add_port (f->a, LRG_GRAPH_PORT_OUTPUT, "vec", "vec3");
	f->b = lrg_node_graph_add_node (f->graph, "b", "Mix", 300.0f, 0.0f);
	f->b_in = lrg_graph_node_add_port (f->b, LRG_GRAPH_PORT_INPUT, "in", "float");
	f->b_extra = lrg_graph_node_add_port (f->b, LRG_GRAPH_PORT_INPUT, "extra", NULL);
	f->c = lrg_node_graph_add_node (f->graph, "c", "Print", 300.0f, 200.0f);
	f->c_in = lrg_graph_node_add_port (f->c, LRG_GRAPH_PORT_INPUT, "in", "string");

	lrg_node_graph_set_view (f->graph, 200.0f, 100.0f, 1.0f, FALSE);
	lrg_canvas_record (f->canvas);
	settle (f->canvas, 10);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->canvas);
}

/* port_xy: canvas position of a port */
static void
port_xy (LrgNodeGraph *graph,
         LrgGraphPort *port,
         gfloat       *x,
         gfloat       *y)
{
	gfloat gx;
	gfloat gy;

	lrg_node_graph_get_port_position (graph, port, &gx, &gy);
	lrg_node_graph_graph_to_screen (graph, gx, gy, x, y);
}

/* node_xy: canvas position of a point inside a node's header */
static void
header_xy (LrgNodeGraph *graph,
           LrgGraphNode *node,
           gfloat       *x,
           gfloat       *y)
{
	lrg_node_graph_graph_to_screen (graph, lrg_graph_node_get_x (node) + 40.0f,
	                                lrg_graph_node_get_y (node) + 12.0f, x, y);
}

/* ==========================================================================
 * Model
 * ========================================================================== */

static void
test_model (Fixture       *f,
            gconstpointer  data)
{
	guint                   added = 0;
	guint                   removed = 0;
	guint                   links_removed = 0;
	LrgGraphNode           *auto_node;
	g_autoptr(LrgGraphNode) loose = NULL;
	GList                  *nodes;

	g_signal_connect_swapped (f->graph, "node-added", G_CALLBACK (count_cb), &added);
	g_signal_connect_swapped (f->graph, "node-removed", G_CALLBACK (count_cb), &removed);
	g_signal_connect_swapped (f->graph, "link-removed", G_CALLBACK (count_cb), &links_removed);

	g_assert_cmpuint (lrg_node_graph_get_n_nodes (f->graph), ==, 3);
	g_assert_true (lrg_node_graph_get_node (f->graph, "b") == f->b);
	g_assert_true (lrg_graph_node_get_graph (f->b) == f->graph);
	g_assert_true (lrg_graph_port_get_node (f->b_in) == f->b);
	g_assert_cmpuint (lrg_graph_node_get_n_ports (f->a, LRG_GRAPH_PORT_OUTPUT), ==, 2);
	g_assert_cmpuint (lrg_graph_node_get_n_ports (f->a, LRG_GRAPH_PORT_INPUT), ==, 0);
	g_assert_true (lrg_graph_node_find_port (f->b, LRG_GRAPH_PORT_INPUT, "extra") == f->b_extra);
	g_assert_null (lrg_graph_node_find_port (f->b, LRG_GRAPH_PORT_OUTPUT, "extra"));
	g_assert_cmpstr (lrg_graph_port_get_type_name (f->a_out), ==, "float");
	g_assert_null (lrg_graph_port_get_type_name (f->b_extra));
	g_assert_true (lrg_graph_port_get_multi_link (f->a_out));
	g_assert_false (lrg_graph_port_get_multi_link (f->b_in));

	/* Automatic ids, duplicate ids refused */
	auto_node = lrg_node_graph_add_node (f->graph, NULL, "Auto", 0.0f, 400.0f);
	g_assert_nonnull (auto_node);
	g_assert_true (g_str_has_prefix (lrg_graph_node_get_id (auto_node), "node-"));
	g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*already has a node*");
	g_assert_null (lrg_node_graph_add_node (f->graph, "a", "Again", 0.0f, 0.0f));
	g_test_assert_expected_messages ();

	/* A pre-built node */
	loose = lrg_graph_node_new ("loose", "Loose");
	lrg_graph_node_add_port (loose, LRG_GRAPH_PORT_INPUT, "x", NULL);
	g_assert_true (lrg_node_graph_append_node (f->graph, loose));
	g_assert_cmpuint (added, ==, 2);

	/* Stacking order */
	nodes = lrg_node_graph_get_nodes (f->graph);
	g_assert_cmpuint (g_list_length (nodes), ==, 5);
	g_assert_true (g_list_last (nodes)->data == loose);
	g_list_free (nodes);

	/* Removing a node removes its links */
	g_assert_nonnull (lrg_node_graph_connect (f->graph, f->a_out, f->b_in, NULL));
	g_assert_nonnull (lrg_node_graph_connect (f->graph, f->a_out, lrg_graph_node_find_port (loose,
	                  LRG_GRAPH_PORT_INPUT, "x"), NULL));
	g_assert_cmpuint (lrg_graph_port_get_n_links (f->a_out), ==, 2);
	lrg_node_graph_remove_node (f->graph, loose);
	g_assert_cmpuint (removed, ==, 1);
	g_assert_cmpuint (links_removed, ==, 1);
	g_assert_cmpuint (lrg_node_graph_get_n_links (f->graph), ==, 1);
	g_assert_null (lrg_graph_node_get_graph (loose));

	/* Removing a port removes its links */
	lrg_graph_node_remove_port (f->b, f->b_in);
	g_assert_cmpuint (lrg_node_graph_get_n_links (f->graph), ==, 0);
	g_assert_cmpuint (lrg_graph_port_get_n_links (f->a_out), ==, 0);
	g_assert_cmpuint (links_removed, ==, 2);

	/* Clear */
	lrg_node_graph_clear (f->graph);
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (f->graph), ==, 0);
	g_assert_cmpuint (removed, ==, 5);
}

/* ==========================================================================
 * Connection rules
 * ========================================================================== */

static gboolean
refuse_vec (LrgNodeGraph *graph,
            LrgGraphPort *output,
            LrgGraphPort *input,
            gpointer      user_data)
{
	(*(guint *) user_data)++;
	/* Allow vec3 -> float (a swizzle), refuse anything into "extra" */
	if (g_strcmp0 (lrg_graph_port_get_name (input), "extra") == 0)
		return FALSE;
	if (lrg_graph_port_get_port_type (output) == g_quark_from_string ("vec3"))
		return TRUE;
	return lrg_graph_port_get_port_type (output) == lrg_graph_port_get_port_type (input);
}

static void
test_connect_validation (Fixture       *f,
                         gconstpointer  data)
{
	g_autoptr(GError) error = NULL;
	LrgGraphLink     *link;
	guint             asked = 0;
	guint             added = 0;

	g_signal_connect_swapped (f->graph, "link-added", G_CALLBACK (count_cb), &added);

	/* Default rule: equal types, or "any" */
	g_assert_true (lrg_node_graph_can_connect (f->graph, f->a_out, f->b_in));
	g_assert_true (lrg_node_graph_can_connect (f->graph, f->a_vec, f->b_extra));
	g_assert_false (lrg_node_graph_can_connect (f->graph, f->a_out, f->c_in));
	g_assert_false (lrg_node_graph_can_connect (f->graph, f->a_vec, f->b_in));

	link = lrg_node_graph_connect (f->graph, f->a_out, f->c_in, &error);
	g_assert_null (link);
	g_assert_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_INCOMPATIBLE);
	g_clear_error (&error);

	/* Structural rules: directions, same node */
	g_assert_null (lrg_node_graph_connect (f->graph, f->b_in, f->a_out, &error));
	g_assert_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_INVALID_PORTS);
	g_clear_error (&error);
	{
		LrgGraphPort *self_in = lrg_graph_node_add_port (f->a, LRG_GRAPH_PORT_INPUT, "loop", "float");

		g_assert_null (lrg_node_graph_connect (f->graph, f->a_out, self_in, &error));
		g_assert_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_INVALID_PORTS);
		g_clear_error (&error);
	}

	/* Duplicates */
	link = lrg_node_graph_connect (f->graph, f->a_out, f->b_in, &error);
	g_assert_no_error (error);
	g_assert_nonnull (link);
	g_assert_true (lrg_graph_link_get_output (link) == f->a_out);
	g_assert_true (lrg_graph_link_get_input (link) == f->b_in);
	g_assert_true (lrg_node_graph_find_link (f->graph, f->a_out, f->b_in) == link);
	g_assert_null (lrg_node_graph_connect (f->graph, f->a_out, f->b_in, &error));
	g_assert_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_EXISTS);
	g_clear_error (&error);
	g_assert_cmpuint (added, ==, 1);

	/* The application's can-connect overrides the default */
	g_signal_connect (f->graph, "can-connect", G_CALLBACK (refuse_vec), &asked);
	g_assert_true (lrg_node_graph_can_connect (f->graph, f->a_vec, f->b_in));
	g_assert_false (lrg_node_graph_can_connect (f->graph, f->a_vec, f->b_extra));
	g_assert_cmpuint (asked, ==, 2);
	g_assert_null (lrg_node_graph_connect (f->graph, f->a_out, f->b_extra, &error));
	g_assert_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_INCOMPATIBLE);
	g_clear_error (&error);

	/* By name */
	g_assert_nonnull (lrg_node_graph_connect_by_name (f->graph, "a", "vec", "b", "in", NULL));
	g_assert_null (lrg_node_graph_connect_by_name (f->graph, "a", "nope", "b", "in", &error));
	g_assert_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_INVALID_PORTS);
}

static void
test_single_input_replace (Fixture       *f,
                           gconstpointer  data)
{
	LrgGraphNode *d;
	LrgGraphPort *d_out;
	LrgGraphLink *first;
	LrgGraphLink *second;
	guint         removed = 0;
	GList        *links;

	g_signal_connect_swapped (f->graph, "link-removed", G_CALLBACK (count_cb), &removed);
	d = lrg_node_graph_add_node (f->graph, "d", "Other", 0.0f, 300.0f);
	d_out = lrg_graph_node_add_port (d, LRG_GRAPH_PORT_OUTPUT, "out", "float");

	/* A single input swaps its link */
	first = lrg_node_graph_connect (f->graph, f->a_out, f->b_in, NULL);
	g_assert_nonnull (first);
	second = lrg_node_graph_connect (f->graph, d_out, f->b_in, NULL);
	g_assert_nonnull (second);
	g_assert_cmpuint (removed, ==, 1);
	g_assert_cmpuint (lrg_node_graph_get_n_links (f->graph), ==, 1);
	g_assert_true (lrg_node_graph_get_nth_link (f->graph, 0) == second);
	g_assert_cmpuint (lrg_graph_port_get_n_links (f->a_out), ==, 0);
	g_assert_cmpuint (lrg_graph_port_get_n_links (f->b_in), ==, 1);

	/* Outputs fan out */
	g_assert_nonnull (lrg_node_graph_connect (f->graph, d_out, f->b_extra, NULL));
	g_assert_cmpuint (lrg_graph_port_get_n_links (d_out), ==, 2);
	g_assert_cmpuint (removed, ==, 1);

	/* A multi-link input keeps both */
	lrg_graph_port_set_multi_link (f->b_in, TRUE);
	g_assert_nonnull (lrg_node_graph_connect (f->graph, f->a_out, f->b_in, NULL));
	g_assert_cmpuint (lrg_graph_port_get_n_links (f->b_in), ==, 2);
	links = lrg_node_graph_get_port_links (f->graph, f->b_in);
	g_assert_cmpuint (g_list_length (links), ==, 2);
	g_list_free (links);

	/* A single-link output replaces too */
	lrg_graph_port_set_multi_link (d_out, FALSE);
	g_assert_cmpuint (lrg_graph_port_get_n_links (d_out), ==, 2);
	g_assert_nonnull (lrg_node_graph_connect (f->graph, d_out,
	                  lrg_graph_node_add_port (f->c, LRG_GRAPH_PORT_INPUT, "any", NULL), NULL));
	g_assert_cmpuint (lrg_graph_port_get_n_links (d_out), ==, 1);
	g_assert_cmpuint (lrg_graph_port_get_n_links (f->b_extra), ==, 0);

	lrg_node_graph_disconnect_port (f->graph, f->b_in);
	g_assert_cmpuint (lrg_graph_port_get_n_links (f->b_in), ==, 0);
}

/* ==========================================================================
 * View maths
 * ========================================================================== */

static void
test_view_math (Fixture       *f,
                gconstpointer  data)
{
	gfloat sx;
	gfloat sy;
	gfloat gx;
	gfloat gy;
	gfloat cx;
	gfloat cy;

	/* Centre (200, 100) at zoom 1: graph (x, y) -> (x + 200, y + 200) */
	lrg_node_graph_graph_to_screen (f->graph, 0.0f, 0.0f, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (sx, 200.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (sy, 200.0f, 0.001f);
	lrg_node_graph_screen_to_graph (f->graph, 400.0f, 300.0f, &gx, &gy);
	g_assert_cmpfloat_with_epsilon (gx, 200.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (gy, 100.0f, 0.001f);

	/* Zoom 2 about a point keeps the point */
	lrg_node_graph_screen_to_graph (f->graph, 100.0f, 150.0f, &gx, &gy);
	lrg_node_graph_zoom_at (f->graph, 100.0f, 150.0f, 2.0f, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_node_graph_get_zoom (f->graph), 2.0f, 0.0001f);
	lrg_node_graph_graph_to_screen (f->graph, gx, gy, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (sx, 100.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (sy, 150.0f, 0.01f);

	/* Round trip at an odd zoom and centre */
	lrg_node_graph_set_view (f->graph, -37.5f, 812.25f, 0.73f, FALSE);
	lrg_node_graph_graph_to_screen (f->graph, 123.0f, -45.0f, &sx, &sy);
	lrg_node_graph_screen_to_graph (f->graph, sx, sy, &gx, &gy);
	g_assert_cmpfloat_with_epsilon (gx, 123.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (gy, -45.0f, 0.01f);
	lrg_node_graph_get_center (f->graph, &cx, &cy);
	g_assert_cmpfloat_with_epsilon (cx, -37.5f, 0.001f);
	g_assert_cmpfloat_with_epsilon (cy, 812.25f, 0.001f);

	/* Clamped to the zoom range */
	lrg_node_graph_set_zoom (f->graph, 50.0f);
	g_assert_cmpfloat_with_epsilon (lrg_node_graph_get_zoom (f->graph),
	                                lrg_node_graph_get_max_zoom (f->graph), 0.0001f);
	lrg_node_graph_set_zoom (f->graph, 0.0001f);
	g_assert_cmpfloat_with_epsilon (lrg_node_graph_get_zoom (f->graph),
	                                lrg_node_graph_get_min_zoom (f->graph), 0.0001f);

	/* Panning moves the content with the pointer */
	lrg_node_graph_set_view (f->graph, 0.0f, 0.0f, 0.5f, FALSE);
	lrg_node_graph_pan_by (f->graph, 50.0f, -20.0f);
	lrg_node_graph_get_center (f->graph, &cx, &cy);
	g_assert_cmpfloat_with_epsilon (cx, -100.0f, 0.001f);
	g_assert_cmpfloat_with_epsilon (cy, 40.0f, 0.001f);
}

/* Wheel zoom glides, and the point under the pointer stays put while it does */
static void
test_wheel_zoom (Fixture       *f,
                 gconstpointer  data)
{
	gfloat gx;
	gfloat gy;
	gfloat sx;
	gfloat sy;
	guint  i;

	lrg_node_graph_screen_to_graph (f->graph, 250.0f, 180.0f, &gx, &gy);
	wheel (f->canvas, 250.0f, 180.0f, 2.0f);
	g_assert_cmpfloat_with_epsilon (lrg_node_graph_get_zoom (f->graph), 1.15f * 1.15f, 0.001f);
	g_assert_cmpfloat (lrg_node_graph_get_displayed_zoom (f->graph), <, 1.15f * 1.15f);

	for (i = 0; i < 4; i++)
	{
		lrg_canvas_update (f->canvas, DT);
		lrg_node_graph_graph_to_screen (f->graph, gx, gy, &sx, &sy);
		g_assert_cmpfloat_with_epsilon (sx, 250.0f, 0.05f);
		g_assert_cmpfloat_with_epsilon (sy, 180.0f, 0.05f);
	}
	settle (f->canvas, 200);
	g_assert_cmpfloat_with_epsilon (lrg_node_graph_get_displayed_zoom (f->graph), 1.15f * 1.15f, 0.001f);
	lrg_node_graph_graph_to_screen (f->graph, gx, gy, &sx, &sy);
	g_assert_cmpfloat_with_epsilon (sx, 250.0f, 0.05f);
}

/* Middle-button drag pans the canvas */
static void
test_middle_pan (Fixture       *f,
                 gconstpointer  data)
{
	gfloat cx;
	gfloat cy;

	move_to (f->canvas, 600.0f, 500.0f);
	press_button (f->canvas, GRL_MOUSE_BUTTON_MIDDLE, 600.0f, 500.0f, 0);
	drag_button (f->canvas, GRL_MOUSE_BUTTON_MIDDLE, 640.0f, 470.0f);
	drag_button (f->canvas, GRL_MOUSE_BUTTON_MIDDLE, 700.0f, 450.0f);
	release_button (f->canvas, GRL_MOUSE_BUTTON_MIDDLE, 700.0f, 450.0f);
	lrg_node_graph_get_center (f->graph, &cx, &cy);
	g_assert_cmpfloat_with_epsilon (cx, 100.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (cy, 150.0f, 0.01f);

	/* The middle button captures: the drag goes on outside the widget
	 * and the release outside ends it */
	press_button (f->canvas, GRL_MOUSE_BUTTON_MIDDLE, 700.0f, 450.0f, 0);
	drag_button (f->canvas, GRL_MOUSE_BUTTON_MIDDLE, 900.0f, 450.0f);
	release_button (f->canvas, GRL_MOUSE_BUTTON_MIDDLE, 900.0f, 450.0f);
	lrg_node_graph_get_center (f->graph, &cx, &cy);
	g_assert_cmpfloat_with_epsilon (cx, -100.0f, 0.01f);
	move_to (f->canvas, 950.0f, 450.0f);
	lrg_node_graph_get_center (f->graph, &cx, &cy);
	g_assert_cmpfloat_with_epsilon (cx, -100.0f, 0.01f);
}

/* key_up: one frame that releases @k with the pointer at (x, y) */
static void
key_up (LrgCanvas *canvas,
        GrlKey     k,
        gfloat     x,
        gfloat     y)
{
	LrgCanvasInput input;

	frame_pointer (&input, x, y);
	lrg_canvas_input_add_key_release (&input, k);
	lrg_canvas_process_input (canvas, &input);
}

/* Space + left drag pans; releasing Space (KEY_UP) disarms it and ends
 * a pan it started, with no device polling */
static void
test_space_pan (Fixture       *f,
                gconstpointer  data)
{
	gfloat cx;
	gfloat cy;
	gfloat cx0;

	/* Focus the graph with a click on empty space, then hold Space */
	click_at (f->canvas, 600.0f, 500.0f, 0);
	lrg_node_graph_get_center (f->graph, &cx0, &cy);
	key (f->canvas, GRL_KEY_SPACE, 0);

	press_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, 600.0f, 500.0f, 0);
	drag_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, 650.0f, 500.0f);
	lrg_node_graph_get_center (f->graph, &cx, &cy);
	g_assert_cmpfloat_with_epsilon (cx, cx0 - 50.0f, 0.01f);

	/* Space up mid-drag: the pan stops following the pointer */
	key_up (f->canvas, GRL_KEY_SPACE, 650.0f, 500.0f);
	drag_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, 750.0f, 500.0f);
	release_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, 750.0f, 500.0f);
	lrg_node_graph_get_center (f->graph, &cx, &cy);
	g_assert_cmpfloat_with_epsilon (cx, cx0 - 50.0f, 0.01f);

	/* Disarmed: a plain left drag on empty space no longer pans */
	drag (f->canvas, 600.0f, 500.0f, 700.0f, 500.0f, 4, 0);
	lrg_node_graph_get_center (f->graph, &cx, &cy);
	g_assert_cmpfloat_with_epsilon (cx, cx0 - 50.0f, 0.01f);

	/* Armed stays armed across drags until Space goes up */
	key (f->canvas, GRL_KEY_SPACE, 0);
	drag (f->canvas, 600.0f, 500.0f, 610.0f, 500.0f, 2, 0);
	drag (f->canvas, 600.0f, 500.0f, 610.0f, 500.0f, 2, 0);
	lrg_node_graph_get_center (f->graph, &cx, &cy);
	g_assert_cmpfloat_with_epsilon (cx, cx0 - 70.0f, 0.01f);
	key_up (f->canvas, GRL_KEY_SPACE, 799.0f, 1.0f);
}

/* ==========================================================================
 * Hit testing
 * ========================================================================== */

static void
test_hit_testing (Fixture       *f,
                  gconstpointer  data)
{
	LrgGraphLink *link;
	gfloat        x;
	gfloat        y;
	gfloat        x1;
	gfloat        y1;
	gfloat        nx;
	gfloat        ny;
	gfloat        nw;
	gfloat        nh;

	lrg_node_graph_get_node_bounds (f->graph, f->a, &nx, &ny, &nw, &nh);
	g_assert_cmpfloat (nw, >=, 140.0f);
	g_assert_cmpfloat (nh, >, 28.0f);

	/* Nodes */
	header_xy (f->graph, f->a, &x, &y);
	g_assert_true (lrg_node_graph_node_at (f->graph, x, y) == f->a);
	g_assert_null (lrg_node_graph_node_at (f->graph, 150.0f, 150.0f));

	/* Overlap: the topmost wins */
	lrg_graph_node_set_position (f->c, 20.0f, 5.0f);
	header_xy (f->graph, f->a, &x, &y);
	g_assert_true (lrg_node_graph_node_at (f->graph, x, y) == f->c);
	lrg_graph_node_set_position (f->c, 300.0f, 200.0f);

	/* Ports, within a few units */
	port_xy (f->graph, f->a_out, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 200.0f + nw, 0.01f);
	g_assert_true (lrg_node_graph_port_at (f->graph, x, y) == f->a_out);
	g_assert_true (lrg_node_graph_port_at (f->graph, x + 5.0f, y - 4.0f) == f->a_out);
	g_assert_null (lrg_node_graph_port_at (f->graph, x + 30.0f, y));
	port_xy (f->graph, f->b_extra, &x, &y);
	g_assert_true (lrg_node_graph_port_at (f->graph, x, y) == f->b_extra);

	/* Links: on the wire midpoint, not far from it */
	link = lrg_node_graph_connect (f->graph, f->a_out, f->c_in, NULL);
	g_assert_null (link);
	link = lrg_node_graph_connect (f->graph, f->a_out, f->b_in, NULL);
	port_xy (f->graph, f->a_out, &x, &y);
	port_xy (f->graph, f->b_in, &x1, &y1);
	g_assert_true (lrg_node_graph_link_at (f->graph, (x + x1) * 0.5f, (y + y1) * 0.5f) == link);
	g_assert_null (lrg_node_graph_link_at (f->graph, (x + x1) * 0.5f, (y + y1) * 0.5f + 40.0f));

	/* Zoomed: hit tests follow the view */
	lrg_node_graph_zoom_at (f->graph, 300.0f, 250.0f, 1.8f, FALSE);
	port_xy (f->graph, f->b_in, &x, &y);
	g_assert_true (lrg_node_graph_port_at (f->graph, x, y) == f->b_in);
	header_xy (f->graph, f->b, &x, &y);
	g_assert_true (lrg_node_graph_node_at (f->graph, x, y) == f->b);
}

/* ==========================================================================
 * Pointer interaction
 * ========================================================================== */

typedef struct
{
	guint         count;
	LrgGraphPort *port;
	gfloat        x;
	gfloat        y;
} DropLog;

static void
on_link_dropped (LrgNodeGraph *graph,
                 LrgGraphPort *port,
                 gfloat        x,
                 gfloat        y,
                 gpointer      user_data)
{
	DropLog *log = user_data;

	log->count++;
	log->port = port;
	log->x = x;
	log->y = y;
}

static void
test_drag_connect (Fixture       *f,
                   gconstpointer  data)
{
	g_autoptr(LrgPaintList) unused = NULL;
	LrgPaintList           *list;
	DropLog                 log = { 0, NULL, 0.0f, 0.0f };
	gfloat                  ox;
	gfloat                  oy;
	gfloat                  ix;
	gfloat                  iy;
	guint                   curves;
	guint                   i;

	g_signal_connect (f->graph, "link-dropped", G_CALLBACK (on_link_dropped), &log);
	port_xy (f->graph, f->a_out, &ox, &oy);
	port_xy (f->graph, f->b_in, &ix, &iy);

	/* Mid-drag: a live wire is painted */
	move_to (f->canvas, ox, oy);
	press_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, ox, oy, 0);
	drag_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, (ox + ix) * 0.5f, oy + 30.0f);
	list = lrg_canvas_record (f->canvas);
	curves = 0;
	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
		if (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, i)) == LRG_PAINT_PRIM_CURVE)
			curves++;
	g_assert_cmpuint (curves, ==, 1);

	/* Released near (not on) a compatible input: snaps and links */
	drag_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, ix - 14.0f, iy + 10.0f);
	release_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, ix - 14.0f, iy + 10.0f);
	g_assert_nonnull (lrg_node_graph_find_link (f->graph, f->a_out, f->b_in));
	g_assert_cmpuint (log.count, ==, 0);

	/* Near an incompatible input (off the card): no link, dropped */
	port_xy (f->graph, f->c_in, &ix, &iy);
	drag (f->canvas, ox, oy, ix - 12.0f, iy, 4, 0);
	g_assert_null (lrg_node_graph_find_link (f->graph, f->a_out, f->c_in));
	g_assert_cmpuint (log.count, ==, 1);
	g_assert_true (log.port == f->a_out);

	/* Dropped on empty canvas: graph coordinates of the drop */
	drag (f->canvas, ox, oy, 700.0f, 550.0f, 4, 0);
	g_assert_cmpuint (log.count, ==, 2);
	g_assert_cmpfloat_with_epsilon (log.x, 500.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (log.y, 350.0f, 0.01f);
	g_assert_cmpuint (lrg_node_graph_get_n_links (f->graph), ==, 1);

	/* Grabbing the input end detaches the wire and re-routes it */
	port_xy (f->graph, f->b_in, &ix, &iy);
	{
		gfloat ex;
		gfloat ey;

		port_xy (f->graph, f->b_extra, &ex, &ey);
		drag (f->canvas, ix, iy, ex + 3.0f, ey + 3.0f, 4, 0);
	}
	g_assert_null (lrg_node_graph_find_link (f->graph, f->a_out, f->b_in));
	g_assert_nonnull (lrg_node_graph_find_link (f->graph, f->a_out, f->b_extra));

	/* ... or drops it */
	port_xy (f->graph, f->b_extra, &ix, &iy);
	drag (f->canvas, ix, iy, 650.0f, 560.0f, 4, 0);
	g_assert_cmpuint (lrg_node_graph_get_n_links (f->graph), ==, 0);
	g_assert_cmpuint (log.count, ==, 3);
	g_assert_true (log.port == f->a_out);

	/* Escape cancels a live wire */
	move_to (f->canvas, ox, oy);
	press_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, ox, oy, 0);
	drag_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, 650.0f, 560.0f);
	key (f->canvas, GRL_KEY_ESCAPE, 0);
	release_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, 650.0f, 560.0f);
	g_assert_cmpuint (log.count, ==, 3);
}

static void
test_drag_move (Fixture       *f,
                gconstpointer  data)
{
	guint  moved = 0;
	gfloat x;
	gfloat y;

	g_signal_connect_swapped (f->graph, "node-moved", G_CALLBACK (count_cb), &moved);

	/* Drag the header: the node follows, node-moved once at the end */
	header_xy (f->graph, f->a, &x, &y);
	drag (f->canvas, x, y, x + 50.0f, y + 20.0f, 5, 0);
	g_assert_cmpfloat_with_epsilon (lrg_graph_node_get_x (f->a), 50.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (lrg_graph_node_get_y (f->a), 20.0f, 0.01f);
	g_assert_cmpuint (moved, ==, 1);
	g_assert_true (lrg_graph_node_get_selected (f->a));

	/* Zoomed out, pointer travel is divided by the zoom */
	lrg_node_graph_zoom_at (f->graph, 400.0f, 300.0f, 0.5f, FALSE);
	header_xy (f->graph, f->a, &x, &y);
	drag (f->canvas, x, y, x + 20.0f, y, 4, 0);
	g_assert_cmpfloat_with_epsilon (lrg_graph_node_get_x (f->a), 90.0f, 0.01f);
	lrg_node_graph_set_view (f->graph, 200.0f, 100.0f, 1.0f, FALSE);

	/* Several selected move together; snap puts the grabbed one on the grid */
	lrg_node_graph_select_node (f->graph, f->b, TRUE);
	lrg_node_graph_set_snap_to_grid (f->graph, TRUE);
	header_xy (f->graph, f->a, &x, &y);
	drag (f->canvas, x, y, x + 13.0f, y + 31.0f, 4, 0);
	g_assert_cmpfloat_with_epsilon (fmodf (lrg_graph_node_get_x (f->a), 24.0f), 0.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (fmodf (lrg_graph_node_get_y (f->a), 24.0f), 0.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (lrg_graph_node_get_x (f->b) - lrg_graph_node_get_x (f->a),
	                                210.0f, 0.01f);
	g_assert_cmpfloat_with_epsilon (lrg_graph_node_get_y (f->b) - lrg_graph_node_get_y (f->a),
	                                -20.0f, 0.01f);

	/* The dragged card lifts (shadow spring), then settles back */
	header_xy (f->graph, f->a, &x, &y);
	move_to (f->canvas, x, y);
	press_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, x, y, 0);
	drag_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, x + 30.0f, y);
	g_assert_true (lrg_canvas_update (f->canvas, DT));
	release_button (f->canvas, GRL_MOUSE_BUTTON_LEFT, x + 30.0f, y);
	settle (f->canvas, 200);
}

static void
test_box_select (Fixture       *f,
                 gconstpointer  data)
{
	guint changes = 0;
	gfloat bx;
	gfloat by;

	g_signal_connect_swapped (f->graph, "selection-changed", G_CALLBACK (count_cb), &changes);

	/* Band from empty space over a and b (both at y 0..), not c (y 200) */
	drag (f->canvas, 150.0f, 150.0f, 560.0f, 230.0f, 6, 0);
	g_assert_true (lrg_graph_node_get_selected (f->a));
	g_assert_true (lrg_graph_node_get_selected (f->b));
	g_assert_false (lrg_graph_node_get_selected (f->c));
	g_assert_cmpuint (lrg_node_graph_get_n_selected (f->graph), ==, 2);
	g_assert_cmpuint (changes, >, 0);

	/* A plain band elsewhere replaces the selection */
	header_xy (f->graph, f->c, &bx, &by);
	drag (f->canvas, bx - 60.0f, by - 30.0f, bx + 10.0f, by + 10.0f, 4, 0);
	g_assert_cmpuint (lrg_node_graph_get_n_selected (f->graph), ==, 1);
	g_assert_true (lrg_graph_node_get_selected (f->c));

	/* Shift extends */
	drag (f->canvas, 150.0f, 150.0f, 260.0f, 230.0f, 4, LRG_INPUT_MODIFIER_SHIFT);
	g_assert_cmpuint (lrg_node_graph_get_n_selected (f->graph), ==, 2);
	g_assert_true (lrg_graph_node_get_selected (f->a));

	/* Ctrl-click toggles a node out; a plain click on empty space clears */
	header_xy (f->graph, f->a, &bx, &by);
	click_at (f->canvas, bx, by, LRG_INPUT_MODIFIER_CTRL);
	g_assert_false (lrg_graph_node_get_selected (f->a));
	click_at (f->canvas, 700.0f, 100.0f, 0);
	g_assert_cmpuint (lrg_node_graph_get_n_selected (f->graph), ==, 0);
}

static void
on_activated (LrgNodeGraph *graph,
              LrgGraphNode *node,
              gpointer      user_data)
{
	*(LrgGraphNode **) user_data = node;
}

static void
test_keyboard (Fixture       *f,
               gconstpointer  data)
{
	LrgGraphNode *activated = NULL;
	guint         dup_signals = 0;
	gfloat        x;
	gfloat        y;

	g_signal_connect (f->graph, "node-activated", G_CALLBACK (on_activated), &activated);
	g_signal_connect_swapped (f->graph, "node-duplicated", G_CALLBACK (count_cb), &dup_signals);
	lrg_node_graph_connect (f->graph, f->a_out, f->b_in, NULL);

	/* Double click activates */
	header_xy (f->graph, f->b, &x, &y);
	click_at (f->canvas, x, y, 0);
	click_at (f->canvas, x, y, 0);
	g_assert_true (activated == f->b);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (f->graph));

	/* Arrows walk the cursor spatially, Enter activates */
	activated = NULL;
	g_assert_true (lrg_node_graph_get_cursor_node (f->graph) == f->b);
	key (f->canvas, GRL_KEY_DOWN, 0);
	g_assert_true (lrg_node_graph_get_cursor_node (f->graph) == f->c);
	g_assert_true (lrg_graph_node_get_selected (f->c));
	key (f->canvas, GRL_KEY_LEFT, 0);
	g_assert_true (lrg_node_graph_get_cursor_node (f->graph) == f->a);
	key (f->canvas, GRL_KEY_LEFT, 0);   /* edge: stays */
	g_assert_true (lrg_node_graph_get_cursor_node (f->graph) == f->a);
	g_assert_true (lrg_canvas_get_focused_widget (f->canvas) == LRG_WIDGET (f->graph));
	key (f->canvas, GRL_KEY_ENTER, 0);
	g_assert_true (activated == f->a);

	/* Combinations the graph does not implement fall through */
	key (f->canvas, GRL_KEY_F, LRG_INPUT_MODIFIER_CTRL);
	key (f->canvas, GRL_KEY_DOWN, LRG_INPUT_MODIFIER_SHIFT);
	g_assert_true (lrg_node_graph_get_cursor_node (f->graph) == f->a);

	/* Ctrl+A, Ctrl+D: copies with their inner link, selected */
	key (f->canvas, GRL_KEY_A, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpuint (lrg_node_graph_get_n_selected (f->graph), ==, 3);
	key (f->canvas, GRL_KEY_D, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (f->graph), ==, 6);
	g_assert_cmpuint (dup_signals, ==, 3);
	g_assert_cmpuint (lrg_node_graph_get_n_links (f->graph), ==, 2);
	g_assert_nonnull (lrg_node_graph_get_node (f->graph, "a-copy"));
	g_assert_false (lrg_graph_node_get_selected (f->a));
	g_assert_true (lrg_graph_node_get_selected (lrg_node_graph_get_node (f->graph, "a-copy")));
	g_assert_cmpfloat_with_epsilon (lrg_graph_node_get_x (lrg_node_graph_get_node (f->graph, "b-copy")),
	                                348.0f, 0.01f);

	/* Delete removes the selection and its links */
	key (f->canvas, GRL_KEY_DELETE, 0);
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (f->graph), ==, 3);
	g_assert_cmpuint (lrg_node_graph_get_n_links (f->graph), ==, 1);

	/* A clicked wire is selected; Delete removes it */
	{
		gfloat x0;
		gfloat y0;
		gfloat x1;
		gfloat y1;

		port_xy (f->graph, f->a_out, &x0, &y0);
		port_xy (f->graph, f->b_in, &x1, &y1);
		click_at (f->canvas, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0);
		g_assert_nonnull (lrg_node_graph_get_selected_link (f->graph));
		key (f->canvas, GRL_KEY_BACKSPACE, 0);
		g_assert_cmpuint (lrg_node_graph_get_n_links (f->graph), ==, 0);
	}
}

/* Home frames every node with a glide that settles */
static void
test_frame_all (Fixture       *f,
                gconstpointer  data)
{
	gfloat x0;
	gfloat y0;
	gfloat x1;
	gfloat y1;
	gfloat nx;
	gfloat ny;
	gfloat nw;
	gfloat nh;
	guint  frames;
	guint  i;

	lrg_node_graph_add_node (f->graph, "far", "Far", 1500.0f, 900.0f);
	lrg_node_graph_set_view (f->graph, -3000.0f, -3000.0f, 1.0f, FALSE);
	lrg_canvas_set_focused_widget (f->canvas, LRG_WIDGET (f->graph));
	key (f->canvas, GRL_KEY_HOME, 0);

	/* It glides: the displayed zoom lags the target for a while */
	g_assert_cmpfloat (fabsf (lrg_node_graph_get_displayed_zoom (f->graph) -
	                          lrg_node_graph_get_zoom (f->graph)), >, 0.01f);
	frames = settle (f->canvas, 300);
	g_assert_cmpuint (frames, >, 5);
	g_assert_cmpfloat_with_epsilon (lrg_node_graph_get_displayed_zoom (f->graph),
	                                lrg_node_graph_get_zoom (f->graph), 0.0001f);
	g_assert_cmpfloat (lrg_node_graph_get_zoom (f->graph), <, 1.0f);

	/* Every card is on screen */
	for (i = 0; i < lrg_node_graph_get_n_nodes (f->graph); i++)
	{
		LrgGraphNode *n = lrg_node_graph_get_nth_node (f->graph, i);

		lrg_node_graph_get_node_bounds (f->graph, n, &nx, &ny, &nw, &nh);
		lrg_node_graph_graph_to_screen (f->graph, nx, ny, &x0, &y0);
		lrg_node_graph_graph_to_screen (f->graph, nx + nw, ny + nh, &x1, &y1);
		g_assert_cmpfloat (x0, >=, 0.0f);
		g_assert_cmpfloat (y0, >=, 0.0f);
		g_assert_cmpfloat (x1, <=, 800.0f);
		g_assert_cmpfloat (y1, <=, 600.0f);
	}

	/* F with a selection frames just it (zoom 1 cap) */
	lrg_node_graph_select_node (f->graph, f->c, FALSE);
	key (f->canvas, GRL_KEY_F, 0);
	settle (f->canvas, 300);
	g_assert_cmpfloat_with_epsilon (lrg_node_graph_get_zoom (f->graph), 1.0f, 0.0001f);
	lrg_node_graph_get_node_bounds (f->graph, f->c, &nx, &ny, &nw, &nh);
	lrg_node_graph_graph_to_screen (f->graph, nx + nw * 0.5f, ny + nh * 0.5f, &x0, &y0);
	g_assert_cmpfloat_with_epsilon (x0, 400.0f, 0.05f);
	g_assert_cmpfloat_with_epsilon (y0, 300.0f, 0.05f);
}

/* ==========================================================================
 * Culling and scale
 * ========================================================================== */

static void
test_culling (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgNodeGraph        *graph = lrg_node_graph_new ();
	LrgPaintList        *list;
	guint                painted_nodes;
	guint                painted_links;
	guint                near_instances;
	guint                i;

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (graph));
	g_object_unref (graph);
	lrg_widget_allocate (LRG_WIDGET (graph), 0.0f, 0.0f, 800.0f, 600.0f);
	lrg_node_graph_set_animate_links (graph, TRUE);

	/* 500 nodes in a 25 x 20 grid, ~1000 links to the right and down */
	for (i = 0; i < 500; i++)
	{
		g_autofree gchar *id = g_strdup_printf ("n%u", i);
		LrgGraphNode     *n = lrg_node_graph_add_node (graph, id, "Node", (gfloat) (i % 25) * 240.0f,
		                                               (gfloat) (i / 25) * 160.0f);

		lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "a", "float");
		lrg_graph_node_add_port (n, LRG_GRAPH_PORT_INPUT, "b", "float");
		lrg_graph_node_add_port (n, LRG_GRAPH_PORT_OUTPUT, "out", "float");
	}
	for (i = 0; i < 500; i++)
	{
		g_autofree gchar *from = g_strdup_printf ("n%u", i);
		g_autofree gchar *right = g_strdup_printf ("n%u", i + 1);
		g_autofree gchar *down = g_strdup_printf ("n%u", i + 25);

		if (i % 25 != 24)
			g_assert_nonnull (lrg_node_graph_connect_by_name (graph, from, "out", right, "a", NULL));
		if (i + 25 < 500)
			g_assert_nonnull (lrg_node_graph_connect_by_name (graph, from, "out", down, "b", NULL));
	}
	g_assert_cmpuint (lrg_node_graph_get_n_links (graph), >=, 950);

	/* Zoom 1 near the origin: a handful of cards and wires painted */
	lrg_node_graph_set_view (graph, 400.0f, 300.0f, 1.0f, FALSE);
	list = lrg_canvas_record (canvas);
	lrg_node_graph_get_paint_stats (graph, &painted_nodes, &painted_links);
	near_instances = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (painted_nodes, >, 4);
	g_assert_cmpuint (painted_nodes, <, 40);
	g_assert_cmpuint (painted_links, >, 4);
	g_assert_cmpuint (painted_links, <, 80);

	/* Far from every node: nothing but the background and grid */
	lrg_node_graph_set_view (graph, -20000.0f, -20000.0f, 1.0f, FALSE);
	list = lrg_canvas_record (canvas);
	lrg_node_graph_get_paint_stats (graph, &painted_nodes, &painted_links);
	g_assert_cmpuint (painted_nodes, ==, 0);
	g_assert_cmpuint (painted_links, ==, 0);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), <, near_instances);

	/* Fully zoomed out: more cards, but text is dropped (LOD) */
	lrg_node_graph_frame_all (graph, FALSE);
	list = lrg_canvas_record (canvas);
	lrg_node_graph_get_paint_stats (graph, &painted_nodes, &painted_links);
	g_assert_cmpfloat_with_epsilon (lrg_node_graph_get_zoom (graph), lrg_node_graph_get_min_zoom (graph),
	                                0.0001f);
	g_assert_cmpuint (painted_nodes, >, 40);
	g_assert_cmpuint (painted_nodes, <=, 500);
	/* Per card: shadow, card, header, outline-free, 3 ports x (dot + ring) */
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), <, painted_nodes * 12 + painted_links + 600);

	/* Hit testing stays correct among 500 nodes */
	{
		LrgGraphNode *n = lrg_node_graph_get_node (graph, "n26");
		gfloat        x;
		gfloat        y;

		lrg_node_graph_set_view (graph, 400.0f, 300.0f, 1.0f, FALSE);
		lrg_node_graph_graph_to_screen (graph, lrg_graph_node_get_x (n) + 20.0f,
		                                lrg_graph_node_get_y (n) + 10.0f, &x, &y);
		g_assert_true (lrg_node_graph_node_at (graph, x, y) == n);
	}
}

/* ==========================================================================
 * Bodies
 * ========================================================================== */

static void
test_body (Fixture       *f,
           gconstpointer  data)
{
	LrgWidget *body = LRG_WIDGET (lrg_button_new ("Go"));
	gfloat     nx;
	gfloat     ny;
	gfloat     nw;
	gfloat     nh;
	gfloat     h_before;
	gfloat     sx;
	gfloat     sy;

	lrg_node_graph_get_node_bounds (f->graph, f->c, NULL, NULL, NULL, &h_before);
	lrg_widget_set_min_size (body, 100.0f, 30.0f);
	lrg_graph_node_set_body (f->c, body);
	g_object_unref (body);
	lrg_canvas_record (f->canvas);

	/* The card grew and the body sits inside it, at the card's screen size */
	g_assert_true (lrg_widget_get_parent (body) == LRG_CONTAINER (f->graph));
	lrg_node_graph_get_node_bounds (f->graph, f->c, &nx, &ny, &nw, &nh);
	g_assert_cmpfloat (nh, >=, h_before + 30.0f);
	lrg_node_graph_graph_to_screen (f->graph, nx, ny, &sx, &sy);
	g_assert_cmpfloat (lrg_widget_get_world_x (body), >, sx);
	g_assert_cmpfloat (lrg_widget_get_world_x (body) + lrg_widget_get_width (body), <, sx + nw);
	g_assert_cmpfloat (lrg_widget_get_world_y (body), >, sy + 28.0f);
	g_assert_cmpfloat (lrg_widget_get_height (body), >=, 30.0f);

	/* The canvas finds it under the pointer */
	g_assert_true (lrg_canvas_widget_at_point (f->canvas,
	               lrg_widget_get_world_x (body) + 5.0f, lrg_widget_get_world_y (body) + 5.0f) == body);

	/* Pressing a card raises its body to the front of the children */
	{
		LrgWidget *other = LRG_WIDGET (lrg_button_new ("Other"));
		gfloat     x;
		gfloat     y;

		lrg_graph_node_set_body (f->a, other);
		g_object_unref (other);
		lrg_canvas_record (f->canvas);
		/* a is below c, so a's body comes first */
		g_assert_true (lrg_container_get_child (LRG_CONTAINER (f->graph), 0) == other);
		header_xy (f->graph, f->a, &x, &y);
		click_at (f->canvas, x, y, 0);
		g_assert_true (lrg_container_get_child (LRG_CONTAINER (f->graph), 1) == other);
		header_xy (f->graph, f->c, &x, &y);
		click_at (f->canvas, x, y, 0);
		g_assert_true (lrg_container_get_child (LRG_CONTAINER (f->graph), 1) == body);
		g_assert_true (lrg_node_graph_get_nth_node (f->graph, 2) == f->c);
	}

	/* Panning carries it along */
	sx = lrg_widget_get_world_x (body);
	lrg_node_graph_pan_by (f->graph, 25.0f, 0.0f);
	g_assert_cmpfloat_with_epsilon (lrg_widget_get_world_x (body), sx + 25.0f, 1.0f);

	/* Too small to use: collapsed, no input */
	lrg_node_graph_set_zoom (f->graph, 0.3f);
	g_assert_cmpfloat (lrg_widget_get_width (body), ==, 0.0f);

	/* Removing the node removes the body child */
	g_object_ref (body);
	lrg_node_graph_remove_node (f->graph, f->c);
	g_assert_null (lrg_widget_get_parent (body));
	g_object_unref (body);
}

/* ==========================================================================
 * YAML
 * ========================================================================== */

static void
test_yaml (Fixture       *f,
           gconstpointer  data)
{
	g_autoptr(LrgNodeGraph) copy = lrg_node_graph_new ();
	g_autoptr(GError)       error = NULL;
	g_autofree gchar       *text = NULL;
	g_autofree gchar       *again = NULL;
	GrlColor                accent = { 10, 200, 30, 255 };
	GrlColor                got;
	LrgGraphNode           *b;
	LrgGraphPort           *p;
	gsize                   len = 0;
	gfloat                  cx;
	gfloat                  cy;

	lrg_graph_node_set_accent (f->a, &accent);
	lrg_graph_node_set_category (f->b, "math");
	lrg_graph_port_set_multi_link (f->b_extra, TRUE);
	lrg_node_graph_connect (f->graph, f->a_out, f->b_in, NULL);
	lrg_node_graph_connect (f->graph, f->a_vec, f->b_extra, NULL);
	lrg_node_graph_set_view (f->graph, 12.5f, -40.0f, 1.5f, FALSE);

	text = lrg_node_graph_to_yaml (f->graph, &len, &error);
	g_assert_no_error (error);
	g_assert_nonnull (text);
	g_assert_cmpuint (len, >, 0);

	g_assert_true (lrg_node_graph_load_yaml (copy, text, -1, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (copy), ==, 3);
	g_assert_cmpuint (lrg_node_graph_get_n_links (copy), ==, 2);
	b = lrg_node_graph_get_node (copy, "b");
	g_assert_nonnull (b);
	g_assert_cmpstr (lrg_graph_node_get_title (b), ==, "Mix");
	g_assert_cmpstr (lrg_graph_node_get_category (b), ==, "math");
	g_assert_cmpfloat_with_epsilon (lrg_graph_node_get_x (b), 300.0f, 0.001f);
	g_assert_true (lrg_graph_node_get_accent (lrg_node_graph_get_node (copy, "a"), &got));
	g_assert_cmpuint (got.g, ==, 200);
	p = lrg_graph_node_find_port (b, LRG_GRAPH_PORT_INPUT, "extra");
	g_assert_nonnull (p);
	g_assert_true (lrg_graph_port_get_multi_link (p));
	g_assert_null (lrg_graph_port_get_type_name (p));
	g_assert_cmpstr (lrg_graph_port_get_type_name (lrg_graph_node_find_port (b, LRG_GRAPH_PORT_INPUT, "in")),
	                 ==, "float");
	g_assert_nonnull (lrg_node_graph_find_link (copy,
	                  lrg_graph_node_find_port (lrg_node_graph_get_node (copy, "a"), LRG_GRAPH_PORT_OUTPUT, "vec"),
	                  p));
	lrg_node_graph_get_center (copy, &cx, &cy);
	g_assert_cmpfloat_with_epsilon (cx, 12.5f, 0.001f);
	g_assert_cmpfloat_with_epsilon (lrg_node_graph_get_zoom (copy), 1.5f, 0.001f);

	/* Stable: saving the copy gives the same document */
	again = lrg_node_graph_to_yaml (copy, NULL, &error);
	g_assert_cmpstr (again, ==, text);

	/* Bad documents fail without touching the graph */
	g_assert_false (lrg_node_graph_load_yaml (copy, "nodes: [ { title: x } ]", -1, &error));
	g_assert_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_PARSE);
	g_clear_error (&error);
	g_assert_false (lrg_node_graph_load_yaml (copy,
	                "nodes:\n  - id: a\n    ports: [ { name: o, direction: output } ]\n"
	                "links:\n  - { from: a, output: o, to: zz, input: i }\n", -1, &error));
	g_assert_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_PARSE);
	g_clear_error (&error);
	g_assert_false (lrg_node_graph_load_yaml (copy, "nodes:\n  - id: a\n  - id: a\n", -1, &error));
	g_clear_error (&error);
	g_assert_cmpuint (lrg_node_graph_get_n_nodes (copy), ==, 3);
	g_assert_cmpuint (lrg_node_graph_get_n_links (copy), ==, 2);
}

/* ==========================================================================
 * Looks
 * ========================================================================== */

/* Every preset paints the graph (selection, live hover, minimap on) */
static void
test_presets_smoke (Fixture       *f,
                    gconstpointer  data)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	g_autoptr(LrgTheme)  theme = lrg_theme_new_preset ("midnight");
	guint                i;

	lrg_widget_set_theme (LRG_WIDGET (f->canvas), theme);
	lrg_node_graph_set_show_minimap (f->graph, TRUE);
	lrg_node_graph_set_grid_style (f->graph, LRG_NODE_GRAPH_GRID_DOTS);
	lrg_node_graph_connect (f->graph, f->a_out, f->b_in, NULL);
	lrg_node_graph_select_node (f->graph, f->a, FALSE);
	for (i = 0; presets[i] != NULL; i++)
	{
		LrgPaintList *list;

		g_assert_true (lrg_theme_apply_preset (theme, presets[i]));
		list = lrg_canvas_record (f->canvas);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 20);
	}
}

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
	test_window = grl_window_new (8, 8, "lrg-ui-node-graph-test");
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

/* GPU: canvas background, card surface and accent header in every preset */
static void
test_gpu_presets (void)
{
	const gchar * const *presets = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas)  canvas = NULL;
	g_autoptr(LrgTheme)   theme = NULL;
	LrgNodeGraph         *graph;
	LrgGraphNode         *node;
	GrlColor              accent = { 0xf3, 0x8b, 0xa8, 255 };
	guint                 i;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = canvas_new ();
	theme = lrg_theme_new_preset ("midnight");
	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	graph = lrg_node_graph_new ();
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (graph));
	g_object_unref (graph);
	lrg_widget_allocate (LRG_WIDGET (graph), 0.0f, 0.0f, (gfloat) GPU_W, (gfloat) GPU_H);
	node = lrg_node_graph_add_node (graph, "n", "Node", 0.0f, -50.0f);
	lrg_graph_node_set_accent (node, &accent);
	lrg_node_graph_set_view (graph, 0.0f, 0.0f, 1.0f, FALSE);

	/* graph (x, y) shows at (128 + x, 64 + y): header rows 14..42,
	 * card body 42..56, background cell centre at graph (-60, -36) */
	for (i = 0; presets[i] != NULL; i++)
	{
		GrlColor bg;
		Image    img;

		lrg_theme_apply_preset (theme, presets[i]);
		lrg_paint_color_mix (lrg_theme_get_color (theme, LRG_THEME_COLOR_WINDOW),
		                     lrg_theme_get_color (theme, LRG_THEME_COLOR_PANEL), 0.5f, &bg);
		img = render_canvas (canvas);
		g_assert_true (pixel_near (&img, 68, 28, &bg, 4));
		g_assert_true (pixel_near (&img, 132, 20, &accent, 10));
		g_assert_true (pixel_near (&img, 200, 50, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE), 4));
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
	g_test_add ("/ui/node-graph/" path, Fixture, NULL, fixture_setup, func, fixture_teardown)

	ADD ("model", test_model);
	ADD ("connect-validation", test_connect_validation);
	ADD ("single-input-replace", test_single_input_replace);
	ADD ("view-math", test_view_math);
	ADD ("wheel-zoom", test_wheel_zoom);
	ADD ("middle-pan", test_middle_pan);
	ADD ("space-pan", test_space_pan);
	ADD ("hit-testing", test_hit_testing);
	ADD ("drag-connect", test_drag_connect);
	ADD ("drag-move", test_drag_move);
	ADD ("box-select", test_box_select);
	ADD ("keyboard", test_keyboard);
	ADD ("frame-all", test_frame_all);
	ADD ("body", test_body);
	ADD ("yaml", test_yaml);
	ADD ("presets", test_presets_smoke);
#undef ADD
	g_test_add_func ("/ui/node-graph/culling", test_culling);
	g_test_add_func ("/ui/node-graph/gpu-presets", test_gpu_presets);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
