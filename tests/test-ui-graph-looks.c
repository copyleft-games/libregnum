/* test-ui-graph-looks.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Canvas looks of LrgNodeGraph and LrgTimelineView: the presets
 * (#LrgCanvasLook), the resolved parts (#LrgCanvasLookParts), override
 * precedence (per item > widget property > part looks > preset >
 * theme), the primitives each link / pin / keyframe shape and grid
 * style records, YAML round trips of the per-item overrides, every
 * preset in every theme, and GPU pixel probes (skipped without a
 * display) for the Blueprint background and the Neon glow.
 */

#include <glib.h>
#include <math.h>
#include <string.h>
#include <libregnum.h>
#include <raylib.h>

#define DT (1.0f / 60.0f)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static LrgCanvas *
canvas_new (void)
{
	LrgCanvas *canvas = lrg_canvas_new ();

	lrg_widget_set_size (LRG_WIDGET (canvas), 800.0f, 600.0f);
	lrg_canvas_set_paint_background (canvas, FALSE);
	return canvas;
}

/* settle: steps the canvas until every animation rests (bounded) */
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
}

/* count_prims: instances of @prim in the canvas' recorded paint list */
static guint
count_prims (LrgPaintList *list,
             LrgPaintPrim  prim)
{
	guint n = 0;
	guint i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
		if (lrg_paint_instance_get_prim (lrg_paint_list_get_instance (list, i)) == prim)
			n++;
	return n;
}

/* count_circles: RECT instances that are circles (square, fully rounded) */
static guint
count_circles (LrgPaintList *list)
{
	guint n = 0;
	guint i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);
		gfloat                  w = inst->rect[2] - inst->rect[0];
		gfloat                  h = inst->rect[3] - inst->rect[1];

		if (lrg_paint_instance_get_prim (inst) == LRG_PAINT_PRIM_RECT && fabsf (w - h) < 0.01f &&
		    inst->radii[0] >= w * 0.5f - 0.01f)
			n++;
	}
	return n;
}

static gboolean
color_eq (const GrlColor *a,
          const GrlColor *b)
{
	return a->r == b->r && a->g == b->g && a->b == b->b && a->a == b->a;
}

/* graph_on: a graph filling an 800 x 600 canvas, view centred on 0, 0 */
static LrgNodeGraph *
graph_on (LrgCanvas *canvas)
{
	LrgNodeGraph *graph = lrg_node_graph_new ();

	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (graph));
	g_object_unref (graph);
	lrg_widget_allocate (LRG_WIDGET (graph), 0.0f, 0.0f, 800.0f, 600.0f);
	lrg_node_graph_set_view (graph, 150.0f, 50.0f, 1.0f, FALSE);
	return graph;
}

/* two_nodes: A (output "out") at 0, 0 and B (input "in") at 300, 100, linked */
static LrgGraphLink *
two_nodes (LrgNodeGraph *graph)
{
	LrgGraphNode *a = lrg_node_graph_add_node (graph, "a", "A", 0.0f, 0.0f);
	LrgGraphNode *b = lrg_node_graph_add_node (graph, "b", "B", 300.0f, 100.0f);

	lrg_graph_node_add_port (a, LRG_GRAPH_PORT_OUTPUT, "out", "float");
	lrg_graph_node_add_port (b, LRG_GRAPH_PORT_INPUT, "in", "float");
	return lrg_node_graph_connect_by_name (graph, "a", "out", "b", "in", NULL);
}

/* timeline_on: a timeline with one track "A" filling the canvas */
static LrgTimelineView *
timeline_on (LrgCanvas             *canvas,
             LrgTimelineViewTrack **track)
{
	LrgTimelineView *tl = lrg_timeline_view_new ();

	lrg_timeline_view_set_duration (tl, 10.0);
	lrg_timeline_view_set_track_height (tl, 40.0f);
	lrg_timeline_view_set_follow_playhead (tl, FALSE);
	*track = lrg_timeline_view_add_track (tl, "A", NULL);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (tl));
	g_object_unref (tl);
	lrg_widget_allocate (LRG_WIDGET (tl), 0.0f, 0.0f, 800.0f, 400.0f);
	settle (canvas);
	return tl;
}

/* ==========================================================================
 * Presets and parts
 * ========================================================================== */

/* Every preset resolves its own parts; signatures of each */
static void
test_presets_distinct (void)
{
	g_autoptr(LrgTheme) theme = lrg_theme_new_preset ("midnight");
	LrgCanvasLookParts  parts[5];
	guint               i;
	guint               j;

	for (i = 0; i < 5; i++)
		lrg_canvas_look_parts_init (&parts[i], (LrgCanvasLook) i, theme);

	/* Pairwise: backgrounds, cards and a whole-struct difference */
	for (i = 0; i < 5; i++)
		for (j = i + 1; j < 5; j++)
		{
			g_assert_false (color_eq (&parts[i].background, &parts[j].background));
			g_assert_false (color_eq (&parts[i].node_background, &parts[j].node_background));
			g_assert_true (memcmp (&parts[i], &parts[j], sizeof (LrgCanvasLookParts)) != 0);
		}

	/* Auto: theme-derived classic look */
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_AUTO].look, ==, LRG_CANVAS_LOOK_AUTO);
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_AUTO].grid_style, ==, LRG_NODE_GRAPH_GRID_LINES);
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_AUTO].header_style, ==, LRG_CANVAS_HEADER_STYLE_GRADIENT);
	g_assert_true (color_eq (&parts[LRG_CANVAS_LOOK_AUTO].node_background,
	                         lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE)));
	g_assert_true (color_eq (&parts[LRG_CANVAS_LOOK_AUTO].accent,
	                         lrg_theme_get_color (theme, LRG_THEME_COLOR_ACCENT)));

	/* Modern: tinted headers, dots, soft deep shadows */
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_MODERN].header_style, ==, LRG_CANVAS_HEADER_STYLE_TINT);
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_MODERN].grid_style, ==, LRG_NODE_GRAPH_GRID_DOTS);
	g_assert_cmpfloat (parts[LRG_CANVAS_LOOK_MODERN].node_shadow_blur, >, 20.0f);

	/* Blueprint: deep blue, fine + coarse lines, amber selection */
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_BLUEPRINT].background.b, >,
	                 parts[LRG_CANVAS_LOOK_BLUEPRINT].background.r + 40);
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_BLUEPRINT].grid_style, ==, LRG_NODE_GRAPH_GRID_LINES);
	g_assert_cmpuint (parts[LRG_CANVAS_LOOK_BLUEPRINT].grid_major_every, >, 1);
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_BLUEPRINT].selection.r, >, parts[LRG_CANVAS_LOOK_BLUEPRINT].selection.b);

	/* Blender: neutral grey, dots, flat muted headers, no glow */
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_BLENDER].background.r, ==, parts[LRG_CANVAS_LOOK_BLENDER].background.g);
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_BLENDER].background.g, ==, parts[LRG_CANVAS_LOOK_BLENDER].background.b);
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_BLENDER].grid_style, ==, LRG_NODE_GRAPH_GRID_DOTS);
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_BLENDER].header_style, ==, LRG_CANVAS_HEADER_STYLE_SOLID);
	g_assert_cmpfloat (parts[LRG_CANVAS_LOOK_BLENDER].header_mute, >, 0.0f);
	g_assert_cmpfloat (parts[LRG_CANVAS_LOOK_BLENDER].link_glow, ==, 0.0f);

	/* Neon: near black, glowing grid, cards and wires, outlined clips */
	g_assert_cmpint (parts[LRG_CANVAS_LOOK_NEON].background.r, <, 16);
	g_assert_cmpfloat (parts[LRG_CANVAS_LOOK_NEON].grid_glow, >, 0.0f);
	g_assert_cmpfloat (parts[LRG_CANVAS_LOOK_NEON].node_glow, >, 0.0f);
	g_assert_true (parts[LRG_CANVAS_LOOK_NEON].node_border_accent);
	g_assert_cmpfloat (parts[LRG_CANVAS_LOOK_NEON].link_glow, >,
	                   parts[LRG_CANVAS_LOOK_BLUEPRINT].link_glow);
	g_assert_true (parts[LRG_CANVAS_LOOK_NEON].clip_outline);

	/* Boxed copy */
	{
		g_autoptr(LrgCanvasLookParts) copy = lrg_canvas_look_parts_copy (&parts[LRG_CANVAS_LOOK_NEON]);

		g_assert_true (memcmp (copy, &parts[LRG_CANVAS_LOOK_NEON], sizeof (LrgCanvasLookParts)) == 0);
	}
}

/* The widgets resolve the preset; Auto follows the theme */
static void
test_widget_parts (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	g_autoptr(LrgTheme)  theme = lrg_theme_new_preset ("daylight");
	LrgNodeGraph        *graph;
	LrgCanvasLookParts   p;
	LrgCanvasLookParts   preset;

	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);
	graph = graph_on (canvas);

	/* Auto: the theme's colours, the classic shapes */
	g_assert_cmpint (lrg_node_graph_get_look (graph), ==, LRG_CANVAS_LOOK_AUTO);
	g_assert_cmpint (lrg_node_graph_get_grid_style (graph), ==, LRG_NODE_GRAPH_GRID_AUTO);
	lrg_node_graph_get_look_parts (graph, &p);
	g_assert_true (color_eq (&p.node_background, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE)));
	g_assert_cmpint (p.grid_style, ==, LRG_NODE_GRAPH_GRID_LINES);
	g_assert_cmpint (p.link_shape, ==, LRG_CANVAS_LINK_SHAPE_BEZIER);
	g_assert_false (p.dark);

	/* Theme change follows */
	lrg_theme_apply_preset (theme, "midnight");
	lrg_node_graph_get_look_parts (graph, &p);
	g_assert_true (color_eq (&p.node_background, lrg_theme_get_color (theme, LRG_THEME_COLOR_SURFACE)));

	/* A preset (by property nick) gives the preset's parts */
	g_object_set (graph, "look", LRG_CANVAS_LOOK_BLUEPRINT, NULL);
	lrg_node_graph_get_look_parts (graph, &p);
	lrg_canvas_look_parts_init (&preset, LRG_CANVAS_LOOK_BLUEPRINT, theme);
	g_assert_cmpint (p.look, ==, LRG_CANVAS_LOOK_BLUEPRINT);
	g_assert_true (color_eq (&p.background, &preset.background));
	g_assert_true (color_eq (&p.background_end, &preset.background_end));
	g_assert_true (color_eq (&p.node_background, &preset.node_background));
	g_assert_true (color_eq (&p.selection, &preset.selection));
	g_assert_cmpfloat (p.link_width, ==, preset.link_width);
	g_assert_cmpfloat (p.node_radius, ==, preset.node_radius);
	g_assert_cmpint (p.grid_style, ==, preset.grid_style);
}

/* per item > widget property > part look > preset */
static void
test_precedence (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgNodeGraph         *graph = graph_on (canvas);
	LrgTimelineViewTrack *track;
	LrgTimelineView      *tl;
	LrgGraphLink         *link;
	LrgCanvasLookParts    p;
	LrgPartLook           over;
	GrlColor              red = { 255, 0, 0, 255 };
	guint                 key;

	link = two_nodes (graph);
	lrg_node_graph_set_look (graph, LRG_CANVAS_LOOK_BLUEPRINT);

	/* --- preset --- */
	lrg_node_graph_get_look_parts (graph, &p);
	g_assert_cmpint (p.link_shape, ==, LRG_CANVAS_LINK_SHAPE_BEZIER);
	g_assert_cmpint (p.pin_shape, ==, LRG_CANVAS_PIN_SHAPE_CIRCLE);
	g_assert_cmpint (p.header_style, ==, LRG_CANVAS_HEADER_STYLE_GRADIENT);
	g_assert_cmpfloat (p.glow, ==, 1.0f);

	/* --- part looks beat the preset --- */
	lrg_part_look_init (&over);
	lrg_part_look_set_background (&over, &red);
	lrg_widget_set_part_look (LRG_WIDGET (graph), LRG_UI_PART_ITEM, LRG_WIDGET_STATE_NONE, &over);
	lrg_node_graph_get_look_parts (graph, &p);
	g_assert_true (color_eq (&p.node_background, &red));

	/* --- widget properties beat the preset --- */
	lrg_node_graph_set_link_shape (graph, LRG_CANVAS_LINK_SHAPE_STEP);
	lrg_node_graph_set_pin_shape (graph, LRG_CANVAS_PIN_SHAPE_SQUARE);
	lrg_node_graph_set_header_style (graph, LRG_CANVAS_HEADER_STYLE_STRIPE);
	lrg_node_graph_set_grid_style (graph, LRG_NODE_GRAPH_GRID_CROSSES);
	lrg_node_graph_set_link_width (graph, 6.0f);
	lrg_node_graph_set_glow (graph, 0.0f);
	lrg_node_graph_get_look_parts (graph, &p);
	g_assert_cmpint (p.link_shape, ==, LRG_CANVAS_LINK_SHAPE_STEP);
	g_assert_cmpint (p.pin_shape, ==, LRG_CANVAS_PIN_SHAPE_SQUARE);
	g_assert_cmpint (p.header_style, ==, LRG_CANVAS_HEADER_STYLE_STRIPE);
	g_assert_cmpint (p.grid_style, ==, LRG_NODE_GRAPH_GRID_CROSSES);
	g_assert_cmpfloat (p.link_width, ==, 6.0f);
	g_assert_cmpfloat (p.glow, ==, 0.0f);

	/* Negative glow returns to the look's; 0 width to the look's */
	lrg_node_graph_set_glow (graph, -5.0f);
	g_assert_cmpfloat (lrg_node_graph_get_glow (graph), ==, -1.0f);
	lrg_node_graph_set_link_width (graph, 0.0f);
	lrg_node_graph_get_look_parts (graph, &p);
	g_assert_cmpfloat (p.glow, ==, 1.0f);
	g_assert_cmpfloat (p.link_width, ==, 3.0f);

	/* --- per item beats the widget (checked in the paint list in
	 *     test_link_shapes / test_pin_shapes; here the accessors) --- */
	lrg_graph_link_set_shape (link, LRG_CANVAS_LINK_SHAPE_STRAIGHT);
	lrg_graph_link_set_color (link, &red);
	g_assert_cmpint (lrg_graph_link_get_shape (link), ==, LRG_CANVAS_LINK_SHAPE_STRAIGHT);
	g_assert_true (lrg_graph_link_get_color (link, &over.background));
	g_assert_true (color_eq (&over.background, &red));
	lrg_graph_link_set_color (link, NULL);
	g_assert_false (lrg_graph_link_get_color (link, NULL));

	/* --- timeline: preset, property, per key --- */
	tl = timeline_on (canvas, &track);
	key = lrg_timeline_view_add_keyframe (tl, track, 1.0, 0.5, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	lrg_timeline_view_set_look (tl, LRG_CANVAS_LOOK_NEON);
	lrg_timeline_view_get_look_parts (tl, &p);
	g_assert_cmpint (p.key_shape, ==, LRG_CANVAS_KEY_SHAPE_CIRCLE);
	g_assert_cmpint (p.grid_style, ==, LRG_NODE_GRAPH_GRID_LINES);
	lrg_timeline_view_set_key_shape (tl, LRG_CANVAS_KEY_SHAPE_BAR);
	lrg_timeline_view_set_grid_style (tl, LRG_NODE_GRAPH_GRID_NONE);
	lrg_timeline_view_get_look_parts (tl, &p);
	g_assert_cmpint (p.key_shape, ==, LRG_CANVAS_KEY_SHAPE_BAR);
	g_assert_cmpint (p.grid_style, ==, LRG_NODE_GRAPH_GRID_NONE);
	lrg_timeline_view_set_keyframe_shape (tl, key, LRG_CANVAS_KEY_SHAPE_SQUARE);
	g_assert_cmpint (lrg_timeline_view_get_keyframe_shape (tl, key), ==, LRG_CANVAS_KEY_SHAPE_SQUARE);

	/* Timeline Auto: no time grid, theme accent playhead */
	lrg_timeline_view_set_look (tl, LRG_CANVAS_LOOK_AUTO);
	lrg_timeline_view_set_grid_style (tl, LRG_NODE_GRAPH_GRID_AUTO);
	lrg_timeline_view_get_look_parts (tl, &p);
	g_assert_cmpint (p.grid_style, ==, LRG_NODE_GRAPH_GRID_NONE);
	g_assert_true (color_eq (&p.playhead, lrg_theme_get_color (lrg_widget_get_theme (LRG_WIDGET (tl)),
	                                                           LRG_THEME_COLOR_ACCENT)));

	/* Clip colour accessor */
	{
		guint    clip = lrg_timeline_view_add_clip (tl, track, 2.0, 1.0, "c");
		GrlColor got;

		g_assert_false (lrg_timeline_view_get_clip_color (tl, clip, &got));
		lrg_timeline_view_set_clip_color (tl, clip, &red);
		g_assert_true (lrg_timeline_view_get_clip_color (tl, clip, &got));
		g_assert_true (color_eq (&got, &red));
	}
}

/* ==========================================================================
 * Shapes in the paint list
 * ========================================================================== */

/* collect_curves: the CURVE instances of the list */
static GPtrArray *
collect_curves (LrgPaintList *list)
{
	GPtrArray *out = g_ptr_array_new ();
	guint      i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);

		if (lrg_paint_instance_get_prim (inst) == LRG_PAINT_PRIM_CURVE)
			g_ptr_array_add (out, (gpointer) inst);
	}
	return out;
}

/* curve_straight: the curve's control points lie on its chord */
static gboolean
curve_straight (const LrgPaintInstance *c)
{
	gfloat dx = c->rect[2] - c->rect[0];
	gfloat dy = c->rect[3] - c->rect[1];
	gfloat len = MAX (1.0f, sqrtf (dx * dx + dy * dy));
	gfloat c1 = fabsf ((c->extra[0] - c->rect[0]) * dy - (c->extra[1] - c->rect[1]) * dx) / len;
	gfloat c2 = fabsf ((c->extra[2] - c->rect[0]) * dy - (c->extra[3] - c->rect[1]) * dx) / len;

	return c1 < 0.01f && c2 < 0.01f;
}

static gboolean
curve_horizontal (const LrgPaintInstance *c)
{
	return fabsf (c->rect[1] - c->rect[3]) < 0.01f;
}

static gboolean
curve_vertical (const LrgPaintInstance *c)
{
	return fabsf (c->rect[0] - c->rect[2]) < 0.01f;
}

/* Each link shape records its curve primitives; hit tests follow */
static void
test_link_shapes (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgNodeGraph        *graph = graph_on (canvas);
	LrgGraphLink        *link = two_nodes (graph);
	g_autoptr(GPtrArray) curves = NULL;
	gfloat               ox;
	gfloat               oy;
	gfloat               ix;
	gfloat               iy;
	guint                i;

	lrg_node_graph_set_grid_style (graph, LRG_NODE_GRAPH_GRID_NONE);
	settle (canvas);
	lrg_node_graph_get_port_position (graph, lrg_graph_link_get_output (link), &ox, &oy);
	lrg_node_graph_get_port_position (graph, lrg_graph_link_get_input (link), &ix, &iy);
	/* to canvas units (hit tests and the paint list use them) */
	lrg_node_graph_graph_to_screen (graph, ox, oy, &ox, &oy);
	lrg_node_graph_graph_to_screen (graph, ix, iy, &ix, &iy);
	g_assert_cmpfloat (ix - ox, >, 100.0f);

	/* --- Bezier: one curve, horizontal tangents, not straight --- */
	curves = collect_curves (lrg_canvas_record (canvas));
	g_assert_cmpuint (curves->len, ==, 1);
	g_assert_false (curve_straight (g_ptr_array_index (curves, 0)));
	g_assert_cmpfloat_with_epsilon (((LrgPaintInstance *) g_ptr_array_index (curves, 0))->extra[1],
	                                ((LrgPaintInstance *) g_ptr_array_index (curves, 0))->rect[1], 0.01f);
	g_clear_pointer (&curves, g_ptr_array_unref);

	/* --- Straight: one collinear curve from port to port --- */
	lrg_node_graph_set_link_shape (graph, LRG_CANVAS_LINK_SHAPE_STRAIGHT);
	curves = collect_curves (lrg_canvas_record (canvas));
	g_assert_cmpuint (curves->len, ==, 1);
	g_assert_true (curve_straight (g_ptr_array_index (curves, 0)));
	g_clear_pointer (&curves, g_ptr_array_unref);

	/* --- Step: three axis-aligned segments --- */
	lrg_node_graph_set_link_shape (graph, LRG_CANVAS_LINK_SHAPE_STEP);
	curves = collect_curves (lrg_canvas_record (canvas));
	g_assert_cmpuint (curves->len, ==, 3);
	for (i = 0; i < curves->len; i++)
	{
		const LrgPaintInstance *c = g_ptr_array_index (curves, i);

		g_assert_true (curve_straight (c));
		g_assert_true (curve_horizontal (c) || curve_vertical (c));
	}
	g_assert_true (curve_vertical (g_ptr_array_index (curves, 1)));
	g_clear_pointer (&curves, g_ptr_array_unref);

	/* The step path is hit at its vertical run, the Bezier one is not
	 * at the same point (it would pass through the middle) */
	{
		gfloat mx = roundf ((ox + ix) * 0.5f);
		gfloat qy = oy + (iy - oy) * 0.15f;
		g_assert_true (lrg_node_graph_link_at (graph, mx, qy) == link);
		lrg_node_graph_set_link_shape (graph, LRG_CANVAS_LINK_SHAPE_STRAIGHT);
		g_assert_null (lrg_node_graph_link_at (graph, mx, qy));
		lrg_node_graph_set_link_shape (graph, LRG_CANVAS_LINK_SHAPE_STEP);
	}

	/* --- Angled: stub, diagonal, stub --- */
	lrg_node_graph_set_link_shape (graph, LRG_CANVAS_LINK_SHAPE_ANGLED);
	curves = collect_curves (lrg_canvas_record (canvas));
	g_assert_cmpuint (curves->len, ==, 3);
	g_assert_true (curve_horizontal (g_ptr_array_index (curves, 0)));
	g_assert_true (curve_horizontal (g_ptr_array_index (curves, 2)));
	g_assert_false (curve_horizontal (g_ptr_array_index (curves, 1)));
	g_assert_false (curve_vertical (g_ptr_array_index (curves, 1)));
	g_clear_pointer (&curves, g_ptr_array_unref);

	/* --- the link's own shape beats the graph's --- */
	lrg_graph_link_set_shape (link, LRG_CANVAS_LINK_SHAPE_STRAIGHT);
	curves = collect_curves (lrg_canvas_record (canvas));
	g_assert_cmpuint (curves->len, ==, 1);
	g_assert_true (curve_straight (g_ptr_array_index (curves, 0)));
	g_clear_pointer (&curves, g_ptr_array_unref);

	/* --- the link's colour paints both ends --- */
	{
		GrlColor                red = { 250, 10, 10, 255 };
		const LrgPaintInstance *c;

		lrg_graph_link_set_color (link, &red);
		curves = collect_curves (lrg_canvas_record (canvas));
		c = g_ptr_array_index (curves, 0);
		g_assert_cmpuint (c->color[0], ==, 250);
		g_assert_cmpuint (c->color2[0], ==, 250);
		g_assert_cmpuint (c->color2[1], ==, 10);
	}

	/* --- selection flow: pulses on the selected wire only --- */
	g_clear_pointer (&curves, g_ptr_array_unref);
	lrg_node_graph_select_link (graph, link);
	curves = collect_curves (lrg_canvas_record (canvas));
	g_assert_cmpfloat (((LrgPaintInstance *) g_ptr_array_index (curves, 0))->data[1], ==, 0.0f);
	g_clear_pointer (&curves, g_ptr_array_unref);
	lrg_node_graph_set_selection_flow (graph, TRUE);
	curves = collect_curves (lrg_canvas_record (canvas));
	g_assert_cmpfloat (((LrgPaintInstance *) g_ptr_array_index (curves, 0))->data[1], >, 0.0f);
}

/* Pin shapes: triangles and circles per shape; Blueprint exec arrows */
static void
test_pin_shapes (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgNodeGraph        *graph = graph_on (canvas);
	LrgGraphNode        *node = lrg_node_graph_add_node (graph, "n", "N", 0.0f, 0.0f);
	LrgGraphPort        *port = lrg_graph_node_add_port (node, LRG_GRAPH_PORT_INPUT, "in", "float");
	LrgPaintList        *list;
	guint                tri0;
	guint                circ0;

	lrg_node_graph_set_grid_style (graph, LRG_NODE_GRAPH_GRID_NONE);
	settle (canvas);

	/* Square: the baseline without port circles or triangles */
	lrg_node_graph_set_pin_shape (graph, LRG_CANVAS_PIN_SHAPE_SQUARE);
	list = lrg_canvas_record (canvas);
	tri0 = count_prims (list, LRG_PAINT_PRIM_TRIANGLE);
	circ0 = count_circles (list);

	/* Circle (free port): the shape plus its hole */
	lrg_node_graph_set_pin_shape (graph, LRG_CANVAS_PIN_SHAPE_CIRCLE);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_circles (list), ==, circ0 + 2);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_TRIANGLE), ==, tri0);

	/* Diamond: two triangles, twice */
	lrg_node_graph_set_pin_shape (graph, LRG_CANVAS_PIN_SHAPE_DIAMOND);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_TRIANGLE), ==, tri0 + 4);

	/* Triangle: one arrow, twice */
	lrg_node_graph_set_pin_shape (graph, LRG_CANVAS_PIN_SHAPE_TRIANGLE);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_TRIANGLE), ==, tri0 + 2);

	/* The port's own shape beats the graph's */
	lrg_graph_port_set_shape (port, LRG_CANVAS_PIN_SHAPE_CIRCLE);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_TRIANGLE), ==, tri0);
	g_assert_cmpuint (count_circles (list), ==, circ0 + 2);

	/* Blueprint draws "exec" ports as arrows unless a shape is set */
	{
		LrgGraphPort *exec = lrg_graph_node_add_port (node, LRG_GRAPH_PORT_OUTPUT, "then", "exec");
		guint         with_exec;

		lrg_graph_port_set_shape (port, LRG_CANVAS_PIN_SHAPE_SQUARE);
		lrg_node_graph_set_pin_shape (graph, LRG_CANVAS_PIN_SHAPE_AUTO);
		lrg_node_graph_set_look (graph, LRG_CANVAS_LOOK_BLUEPRINT);
		list = lrg_canvas_record (canvas);
		with_exec = count_prims (list, LRG_PAINT_PRIM_TRIANGLE);
		lrg_graph_port_set_shape (exec, LRG_CANVAS_PIN_SHAPE_CIRCLE);
		list = lrg_canvas_record (canvas);
		g_assert_cmpuint (with_exec, ==, count_prims (list, LRG_PAINT_PRIM_TRIANGLE) + 2);
	}
}

/* find_key_rect: a rect centred at @x taller than twice its width */
static gboolean
find_bar (LrgPaintList *list,
          gfloat        x)
{
	guint i;

	for (i = 0; i < lrg_paint_list_get_n_instances (list); i++)
	{
		const LrgPaintInstance *inst = lrg_paint_list_get_instance (list, i);
		gfloat                  w = inst->rect[2] - inst->rect[0];
		gfloat                  h = inst->rect[3] - inst->rect[1];

		if (lrg_paint_instance_get_prim (inst) == LRG_PAINT_PRIM_RECT &&
		    fabsf ((inst->rect[0] + inst->rect[2]) * 0.5f - x) < 0.5f && h > w * 2.0f && w < 6.0f)
			return TRUE;
	}
	return FALSE;
}

/* Keyframe shapes in the clip lane */
static void
test_key_shapes (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgTimelineViewTrack *track;
	LrgTimelineView      *tl = timeline_on (canvas, &track);
	LrgPaintList         *list;
	guint                 key;
	guint                 tri;
	guint                 circ;
	gfloat                kx;

	key = lrg_timeline_view_add_keyframe (tl, track, 1.0, 0.5, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	settle (canvas);
	kx = lrg_timeline_view_time_to_x (tl, 1.0);

	/* Square: baseline (no triangles or circles of its own) */
	lrg_timeline_view_set_key_shape (tl, LRG_CANVAS_KEY_SHAPE_SQUARE);
	list = lrg_canvas_record (canvas);
	tri = count_prims (list, LRG_PAINT_PRIM_TRIANGLE);
	circ = count_circles (list);
	g_assert_false (find_bar (list, kx));

	/* Diamond (the Auto look's): outline and fill, two triangles each */
	lrg_timeline_view_set_key_shape (tl, LRG_CANVAS_KEY_SHAPE_AUTO);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_TRIANGLE), ==, tri + 4);

	/* Circle: outline and fill dots */
	lrg_timeline_view_set_key_shape (tl, LRG_CANVAS_KEY_SHAPE_CIRCLE);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_circles (list), ==, circ + 2);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_TRIANGLE), ==, tri);

	/* Bar: a thin tall rect at the key */
	lrg_timeline_view_set_key_shape (tl, LRG_CANVAS_KEY_SHAPE_BAR);
	list = lrg_canvas_record (canvas);
	g_assert_true (find_bar (list, kx));

	/* The key's own shape beats the timeline's */
	lrg_timeline_view_set_keyframe_shape (tl, key, LRG_CANVAS_KEY_SHAPE_DIAMOND);
	list = lrg_canvas_record (canvas);
	g_assert_false (find_bar (list, kx));
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_TRIANGLE), ==, tri + 4);
}

/* Grid styles: lines, dots, crosses, none; glow halos; timeline grid */
static void
test_grid_styles (void)
{
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	LrgNodeGraph         *graph = graph_on (canvas);
	LrgPaintList         *list;
	guint                 lines;
	guint                 plain;

	settle (canvas);

	/* Auto look, auto grid: lines */
	list = lrg_canvas_record (canvas);
	lines = count_prims (list, LRG_PAINT_PRIM_LINE);
	g_assert_cmpuint (lines, >, 10);
	g_assert_cmpuint (count_circles (list), ==, 0);

	lrg_node_graph_set_grid_style (graph, LRG_NODE_GRAPH_GRID_DOTS);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_LINE), ==, 0);
	g_assert_cmpuint (count_circles (list), >, 10);

	lrg_node_graph_set_grid_style (graph, LRG_NODE_GRAPH_GRID_CROSSES);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_LINE), >, 10);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_LINE) % 2, ==, 0);
	g_assert_cmpuint (count_circles (list), ==, 0);

	lrg_node_graph_set_grid_style (graph, LRG_NODE_GRAPH_GRID_NONE);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_LINE), ==, 0);
	g_assert_cmpuint (count_circles (list), ==, 0);

	/* Presets pick their grid: Blender dots, Blueprint lines */
	lrg_node_graph_set_grid_style (graph, LRG_NODE_GRAPH_GRID_AUTO);
	lrg_node_graph_set_look (graph, LRG_CANVAS_LOOK_BLENDER);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_circles (list), >, 10);
	lrg_node_graph_set_look (graph, LRG_CANVAS_LOOK_BLUEPRINT);
	list = lrg_canvas_record (canvas);
	g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_LINE), >, 10);

	/* Neon: major lines carry a halo line, glow 0 removes it */
	lrg_node_graph_set_look (graph, LRG_CANVAS_LOOK_NEON);
	list = lrg_canvas_record (canvas);
	lines = count_prims (list, LRG_PAINT_PRIM_LINE);
	lrg_node_graph_set_glow (graph, 0.0f);
	list = lrg_canvas_record (canvas);
	plain = count_prims (list, LRG_PAINT_PRIM_LINE);
	g_assert_cmpuint (lines, >, plain);

	/* Timeline: no grid in Auto, lines on request */
	lrg_container_remove_child (LRG_CONTAINER (canvas), LRG_WIDGET (graph));
	{
		LrgTimelineViewTrack *track;
		LrgTimelineView      *tl = timeline_on (canvas, &track);

		list = lrg_canvas_record (canvas);
		plain = count_prims (list, LRG_PAINT_PRIM_LINE);
		lrg_timeline_view_set_grid_style (tl, LRG_NODE_GRAPH_GRID_LINES);
		list = lrg_canvas_record (canvas);
		g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_LINE), >, plain + 5);
		lrg_timeline_view_set_grid_style (tl, LRG_NODE_GRAPH_GRID_DOTS);
		list = lrg_canvas_record (canvas);
		g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_LINE), ==, plain);
	}
}

/* Node icons: built-in by name, widen the card, unknown names draw nothing */
static void
test_node_icons (void)
{
	g_autoptr(LrgCanvas) canvas = canvas_new ();
	LrgNodeGraph        *graph = graph_on (canvas);
	LrgGraphNode        *node = lrg_node_graph_add_node (graph, "n", "A rather long title", 0.0f, 0.0f);
	gfloat               w0;
	gfloat               w1;
	gfloat               x;
	gfloat               y;
	gfloat               h;
	guint                n0;
	guint                n1;

	lrg_node_graph_set_grid_style (graph, LRG_NODE_GRAPH_GRID_NONE);
	settle (canvas);
	lrg_node_graph_get_node_bounds (graph, node, &x, &y, &w0, &h);
	n0 = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));

	lrg_graph_node_set_icon (node, "gear");
	g_assert_cmpstr (lrg_graph_node_get_icon (node), ==, "gear");
	lrg_node_graph_get_node_bounds (graph, node, &x, &y, &w1, &h);
	n1 = lrg_paint_list_get_n_instances (lrg_canvas_record (canvas));
	g_assert_cmpfloat (w1, >, w0);
	g_assert_cmpuint (n1, >, n0);

	lrg_graph_node_set_icon (node, "no-such-icon");
	g_assert_cmpstr (lrg_graph_node_get_icon (node), ==, "no-such-icon");
	g_assert_cmpuint (lrg_paint_list_get_n_instances (lrg_canvas_record (canvas)), ==, n0);
	lrg_graph_node_set_icon (node, "");
	g_assert_null (lrg_graph_node_get_icon (node));
}

/* ==========================================================================
 * YAML
 * ========================================================================== */

static void
test_yaml_graph (void)
{
	g_autoptr(LrgNodeGraph) a = lrg_node_graph_new ();
	g_autoptr(LrgNodeGraph) b = lrg_node_graph_new ();
	g_autoptr(GError)       error = NULL;
	g_autofree gchar       *doc = NULL;
	g_autofree gchar       *again = NULL;
	LrgGraphLink           *link;
	LrgGraphNode           *n;
	GrlColor                teal = { 0x22, 0xd3, 0xee, 0xff };
	GrlColor                got;

	link = two_nodes (a);
	lrg_graph_node_set_icon (lrg_node_graph_get_node (a, "a"), "play");
	lrg_graph_port_set_shape (lrg_graph_link_get_input (link), LRG_CANVAS_PIN_SHAPE_DIAMOND);
	lrg_graph_link_set_color (link, &teal);
	lrg_graph_link_set_shape (link, LRG_CANVAS_LINK_SHAPE_STEP);

	doc = lrg_node_graph_to_yaml (a, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (doc, "icon: play"));
	g_assert_nonnull (strstr (doc, "shape: step"));
	g_assert_nonnull (strstr (doc, "shape: diamond"));

	g_assert_true (lrg_node_graph_load_yaml (b, doc, -1, &error));
	g_assert_no_error (error);
	n = lrg_node_graph_get_node (b, "a");
	g_assert_cmpstr (lrg_graph_node_get_icon (n), ==, "play");
	g_assert_null (lrg_graph_node_get_icon (lrg_node_graph_get_node (b, "b")));
	link = lrg_node_graph_get_nth_link (b, 0);
	g_assert_cmpint (lrg_graph_link_get_shape (link), ==, LRG_CANVAS_LINK_SHAPE_STEP);
	g_assert_true (lrg_graph_link_get_color (link, &got));
	g_assert_true (color_eq (&got, &teal));
	g_assert_cmpint (lrg_graph_port_get_shape (lrg_graph_link_get_input (link)), ==, LRG_CANVAS_PIN_SHAPE_DIAMOND);
	g_assert_cmpint (lrg_graph_port_get_shape (lrg_graph_link_get_output (link)), ==, LRG_CANVAS_PIN_SHAPE_AUTO);

	/* Saving the loaded graph gives the same document */
	again = lrg_node_graph_to_yaml (b, NULL, &error);
	g_assert_cmpstr (again, ==, doc);

	/* Unknown shape: the whole load fails and nothing changes */
	g_assert_false (lrg_node_graph_load_yaml (b,
	                "nodes:\n"
	                "  - {id: x, ports: [{name: o, direction: output}]}\n"
	                "  - {id: y, ports: [{name: i, direction: input}]}\n"
	                "links:\n"
	                "  - {from: x, output: o, to: y, input: i, shape: wiggly}\n", -1, &error));
	g_assert_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_PARSE);
	g_clear_error (&error);
	g_assert_nonnull (lrg_node_graph_get_node (b, "a"));
	g_assert_false (lrg_node_graph_load_yaml (b,
	                "nodes:\n"
	                "  - {id: x, ports: [{name: o, direction: output, shape: blob}]}\n", -1, &error));
	g_assert_error (error, LRG_NODE_GRAPH_ERROR, LRG_NODE_GRAPH_ERROR_PARSE);
	g_assert_nonnull (lrg_node_graph_get_node (b, "a"));
}

static void
test_yaml_timeline (void)
{
	g_autoptr(LrgTimelineView) a = lrg_timeline_view_new ();
	g_autoptr(LrgTimelineView) b = lrg_timeline_view_new ();
	g_autoptr(GError)          error = NULL;
	g_autofree gchar          *doc = NULL;
	g_autofree gchar          *again = NULL;
	LrgTimelineViewTrack      *group;
	LrgTimelineViewTrack      *child;
	LrgTimelineViewTrack      *t;
	GrlColor                   red = { 0xff, 0x40, 0x40, 0xff };
	GrlColor                   got;
	guint                      clip;
	guint                      key;

	lrg_timeline_view_set_duration (a, 12.0);
	lrg_timeline_view_set_fps (a, 24.0);
	group = lrg_timeline_view_add_track (a, "Group", NULL);
	lrg_timeline_view_track_set_color (group, &red);
	child = lrg_timeline_view_add_track (a, "Child", group);
	lrg_timeline_view_track_set_curve_lane (child, TRUE);
	lrg_timeline_view_track_set_muted (child, TRUE);
	clip = lrg_timeline_view_add_clip (a, child, 1.0, 2.5, "intro");
	lrg_timeline_view_set_clip_color (a, clip, &red);
	lrg_timeline_view_add_clip (a, child, 4.0, 1.0, NULL);
	key = lrg_timeline_view_add_keyframe (a, child, 0.5, 0.25, LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER);
	lrg_timeline_view_set_keyframe_tangents (a, key, 0.5, -1.5);
	lrg_timeline_view_set_keyframe_shape (a, key, LRG_CANVAS_KEY_SHAPE_BAR);
	lrg_timeline_view_add_keyframe (a, child, 3.0, 0.75, LRG_TIMELINE_VIEW_INTERPOLATION_STEP);
	lrg_timeline_view_add_marker (a, 6.0, "cue");

	doc = lrg_timeline_view_to_yaml (a, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (doc, "shape: bar"));

	/* b has content of its own: the load replaces it */
	lrg_timeline_view_add_track (b, "old", NULL);
	lrg_timeline_view_add_marker (b, 1.0, "old");
	g_assert_true (lrg_timeline_view_load_yaml (b, doc, -1, &error));
	g_assert_no_error (error);

	g_assert_cmpfloat (lrg_timeline_view_get_duration (b), ==, 12.0);
	g_assert_cmpfloat (lrg_timeline_view_get_fps (b), ==, 24.0);
	g_assert_cmpuint (lrg_timeline_view_get_n_tracks (b), ==, 2);
	g_assert_cmpuint (lrg_timeline_view_get_n_markers (b), ==, 1);
	t = lrg_timeline_view_get_track (b, 0);
	g_assert_cmpstr (lrg_timeline_view_track_get_name (t), ==, "Group");
	lrg_timeline_view_track_get_color (t, &got);
	g_assert_true (color_eq (&got, &red));
	t = lrg_timeline_view_get_track (b, 1);
	g_assert_true (lrg_timeline_view_track_get_parent (t) == lrg_timeline_view_get_track (b, 0));
	g_assert_true (lrg_timeline_view_track_get_curve_lane (t));
	g_assert_true (lrg_timeline_view_track_get_muted (t));
	g_assert_cmpuint (lrg_timeline_view_get_n_clips (b, t), ==, 2);
	clip = lrg_timeline_view_get_clip_at (b, t, 2.0);
	g_assert_cmpstr (lrg_timeline_view_get_clip_label (b, clip), ==, "intro");
	g_assert_true (lrg_timeline_view_get_clip_color (b, clip, &got));
	g_assert_true (color_eq (&got, &red));
	g_assert_cmpuint (lrg_timeline_view_get_n_keyframes (b, t), ==, 2);
	key = lrg_timeline_view_get_keyframe (b, t, 0);
	g_assert_cmpint (lrg_timeline_view_get_keyframe_shape (b, key), ==, LRG_CANVAS_KEY_SHAPE_BAR);
	g_assert_cmpint (lrg_timeline_view_get_keyframe_interpolation (b, key), ==,
	                 LRG_TIMELINE_VIEW_INTERPOLATION_BEZIER);
	key = lrg_timeline_view_get_keyframe (b, t, 1);
	g_assert_cmpint (lrg_timeline_view_get_keyframe_shape (b, key), ==, LRG_CANVAS_KEY_SHAPE_AUTO);

	/* Saving again gives the same document */
	again = lrg_timeline_view_to_yaml (b, NULL, &error);
	g_assert_cmpstr (again, ==, doc);

	/* A bad key shape fails the whole load; nothing changes */
	g_assert_false (lrg_timeline_view_load_yaml (b,
	                "tracks:\n"
	                "  - name: X\n"
	                "    keyframes: [{time: 1, value: 0, shape: star}]\n", -1, &error));
	g_assert_error (error, LRG_TIMELINE_VIEW_ERROR, LRG_TIMELINE_VIEW_ERROR_PARSE);
	g_clear_error (&error);
	g_assert_cmpuint (lrg_timeline_view_get_n_tracks (b), ==, 2);
	g_assert_false (lrg_timeline_view_load_yaml (b, "tracks:\n  - {name: X, wobble: 3}\n", -1, &error));
	g_assert_error (error, LRG_TIMELINE_VIEW_ERROR, LRG_TIMELINE_VIEW_ERROR_PARSE);
	g_assert_cmpuint (lrg_timeline_view_get_n_tracks (b), ==, 2);
}

/* ==========================================================================
 * Every preset in every theme
 * ========================================================================== */

static void
test_all_presets_all_themes (void)
{
	const gchar * const  *themes = lrg_theme_list_presets ();
	g_autoptr(LrgCanvas)  canvas = canvas_new ();
	g_autoptr(LrgTheme)   theme = lrg_theme_new_preset ("midnight");
	LrgNodeGraph         *graph;
	LrgTimelineView      *tl;
	LrgTimelineViewTrack *track;
	LrgGraphLink         *link;
	guint                 i;
	guint                 look;

	lrg_widget_set_theme (LRG_WIDGET (canvas), theme);

	/* A graph with a selected node, an icon, a free and a linked port */
	graph = lrg_node_graph_new ();
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (graph));
	g_object_unref (graph);
	lrg_widget_allocate (LRG_WIDGET (graph), 0.0f, 0.0f, 800.0f, 300.0f);
	link = two_nodes (graph);
	lrg_graph_node_set_icon (lrg_node_graph_get_node (graph, "a"), "gear");
	lrg_graph_node_set_category (lrg_node_graph_get_node (graph, "b"), "math");
	lrg_graph_node_add_port (lrg_node_graph_get_node (graph, "b"), LRG_GRAPH_PORT_INPUT, "free", NULL);
	lrg_node_graph_select_node (graph, lrg_node_graph_get_node (graph, "a"), FALSE);
	lrg_node_graph_set_show_minimap (graph, TRUE);
	lrg_node_graph_set_view (graph, 150.0f, 50.0f, 1.0f, FALSE);
	(void) link;

	/* A timeline with a clip, keyframes, a marker and a loop */
	tl = lrg_timeline_view_new ();
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (tl));
	g_object_unref (tl);
	lrg_widget_allocate (LRG_WIDGET (tl), 0.0f, 300.0f, 800.0f, 300.0f);
	track = lrg_timeline_view_add_track (tl, "A", NULL);
	lrg_timeline_view_track_set_curve_lane (track, TRUE);
	lrg_timeline_view_select_clip (tl, lrg_timeline_view_add_clip (tl, track, 0.5, 1.0, "clip"), FALSE);
	lrg_timeline_view_add_clip (tl, track, 2.0, 1.0, "other");
	lrg_timeline_view_add_keyframe (tl, track, 0.2, 0.1, LRG_TIMELINE_VIEW_INTERPOLATION_EASE);
	lrg_timeline_view_add_keyframe (tl, track, 1.8, 0.9, LRG_TIMELINE_VIEW_INTERPOLATION_LINEAR);
	lrg_timeline_view_add_marker (tl, 2.5, "cue");
	lrg_timeline_view_set_loop (tl, TRUE);
	lrg_timeline_view_set_loop_range (tl, 1.0, 2.0);
	lrg_timeline_view_set_time (tl, 1.2);

	for (i = 0; themes[i] != NULL; i++)
	{
		lrg_theme_apply_preset (theme, themes[i]);
		for (look = LRG_CANVAS_LOOK_AUTO; look <= LRG_CANVAS_LOOK_NEON; look++)
		{
			LrgCanvasLookParts p;
			LrgPaintList      *list;

			lrg_node_graph_set_look (graph, (LrgCanvasLook) look);
			lrg_timeline_view_set_look (tl, (LrgCanvasLook) look);
			settle (canvas);
			list = lrg_canvas_record (canvas);
			g_assert_cmpuint (count_prims (list, LRG_PAINT_PRIM_CURVE), >=, 1);
			g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 60);

			/* Resolved parts are never "auto" */
			lrg_node_graph_get_look_parts (graph, &p);
			g_assert_cmpint (p.grid_style, !=, LRG_NODE_GRAPH_GRID_AUTO);
			g_assert_cmpint (p.link_shape, !=, LRG_CANVAS_LINK_SHAPE_AUTO);
			g_assert_cmpint (p.pin_shape, !=, LRG_CANVAS_PIN_SHAPE_AUTO);
			g_assert_cmpint (p.header_style, !=, LRG_CANVAS_HEADER_STYLE_AUTO);
			lrg_timeline_view_get_look_parts (tl, &p);
			g_assert_cmpint (p.key_shape, !=, LRG_CANVAS_KEY_SHAPE_AUTO);
			g_assert_cmpint (p.grid_style, !=, LRG_NODE_GRAPH_GRID_AUTO);
		}
	}
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

#define GPU_W (400)
#define GPU_H (200)

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
	test_window = grl_window_new (8, 8, "lrg-ui-graph-looks-test");
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

/* gpu_graph: a GPU-sized canvas with one node centred at 200, 100 */
static LrgNodeGraph *
gpu_graph (LrgCanvas     *canvas,
           LrgCanvasLook  look)
{
	LrgNodeGraph *graph = lrg_node_graph_new ();
	LrgGraphNode *node;
	GrlColor      cyan = { 0x22, 0xd3, 0xee, 255 };

	lrg_widget_set_size (LRG_WIDGET (canvas), (gfloat) GPU_W, (gfloat) GPU_H);
	lrg_container_add_child (LRG_CONTAINER (canvas), LRG_WIDGET (graph));
	g_object_unref (graph);
	lrg_widget_allocate (LRG_WIDGET (graph), 0.0f, 0.0f, (gfloat) GPU_W, (gfloat) GPU_H);
	lrg_node_graph_set_look (graph, look);

	/* Card 140 x ~42 at graph (-70, -20): screen x 130..270, y 80..122 */
	node = lrg_node_graph_add_node (graph, "n", "Node", -70.0f, -20.0f);
	lrg_graph_node_set_accent (node, &cyan);
	lrg_node_graph_set_view (graph, 0.0f, 0.0f, 1.0f, FALSE);
	settle (canvas);
	return graph;
}

/* GPU: Blueprint paints a deep blue board, its header the node colour */
static void
test_gpu_blueprint (void)
{
	g_autoptr(LrgCanvas) canvas = NULL;
	LrgCanvasLookParts   p;
	LrgNodeGraph        *graph;
	Image                img;
	Color                bg;
	Color                header;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = canvas_new ();
	graph = gpu_graph (canvas, LRG_CANVAS_LOOK_BLUEPRINT);
	lrg_node_graph_get_look_parts (graph, &p);
	img = render_canvas (canvas);

	/* Background away from the card and off the grid lines */
	bg = GetImageColor (img, 30, 37);
	g_assert_cmpint ((gint) bg.b, >, (gint) bg.r + 40);
	g_assert_cmpint ((gint) bg.b, >, 60);
	g_assert_cmpint (ABS ((gint) bg.b - (gint) p.background.b), <, 40);

	/* Header: the gradient starts at the node's cyan */
	header = GetImageColor (img, 136, 92);
	g_assert_cmpint (ABS ((gint) header.g - 0xd3), <, 30);
	g_assert_cmpint (ABS ((gint) header.b - 0xee), <, 30);
	UnloadImage (img);
}

/* GPU: Neon is near black and the card glows in its colour */
static void
test_gpu_neon (void)
{
	g_autoptr(LrgCanvas) canvas = NULL;
	LrgNodeGraph        *graph;
	Image                img;
	Color                bg;
	Color                lit;
	Color                dark;

	if (!graphics_available)
	{
		g_test_skip ("Graphics context not available");
		return;
	}

	canvas = canvas_new ();
	graph = gpu_graph (canvas, LRG_CANVAS_LOOK_NEON);
	lrg_node_graph_set_grid_style (graph, LRG_NODE_GRAPH_GRID_NONE);

	img = render_canvas (canvas);
	bg = GetImageColor (img, 20, 20);
	g_assert_cmpint ((gint) bg.r + (gint) bg.g + (gint) bg.b, <, 60);

	/* Just left of the card: lit by the cyan glow */
	lit = GetImageColor (img, 125, 101);
	g_assert_cmpint ((gint) lit.g + (gint) lit.b, >, (gint) bg.g + (gint) bg.b + 30);
	g_assert_cmpint ((gint) lit.b, >, (gint) lit.r);
	UnloadImage (img);

	/* Glow off: the same pixel is background */
	lrg_node_graph_set_glow (graph, 0.0f);
	img = render_canvas (canvas);
	dark = GetImageColor (img, 125, 101);
	g_assert_cmpint ((gint) dark.g + (gint) dark.b, <, (gint) lit.g + (gint) lit.b - 20);
	UnloadImage (img);
}

int
main (int    argc,
      char **argv)
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/ui/graph-looks/presets-distinct", test_presets_distinct);
	g_test_add_func ("/ui/graph-looks/widget-parts", test_widget_parts);
	g_test_add_func ("/ui/graph-looks/precedence", test_precedence);
	g_test_add_func ("/ui/graph-looks/link-shapes", test_link_shapes);
	g_test_add_func ("/ui/graph-looks/pin-shapes", test_pin_shapes);
	g_test_add_func ("/ui/graph-looks/key-shapes", test_key_shapes);
	g_test_add_func ("/ui/graph-looks/grid-styles", test_grid_styles);
	g_test_add_func ("/ui/graph-looks/node-icons", test_node_icons);
	g_test_add_func ("/ui/graph-looks/yaml-graph", test_yaml_graph);
	g_test_add_func ("/ui/graph-looks/yaml-timeline", test_yaml_timeline);
	g_test_add_func ("/ui/graph-looks/all-presets-all-themes", test_all_presets_all_themes);
	g_test_add_func ("/ui/gpu/graph-looks/blueprint", test_gpu_blueprint);
	g_test_add_func ("/ui/gpu/graph-looks/neon", test_gpu_neon);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
