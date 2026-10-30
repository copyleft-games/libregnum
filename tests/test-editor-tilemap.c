/* test-editor-tilemap.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for tilemap painting: the LrgNodeTilemap grid model,
 * flood regions, LrgEditorTilePainter strokes/rect/fill/eyedropper with
 * their undo grouping, world -> cell mapping, and the .rlevel round trip.
 */

#include <glib.h>
#include <string.h>
#include <libregnum.h>

/* A level with one tilemap node; the editor owns everything. */
typedef struct
{
	LrgEditor            *editor;
	LrgEditorTilePainter *painter;
	LrgNode              *node;   /* borrowed from the level */
} Fixture;

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	g_autoptr(LrgNode)       node = lrg_node_new ("Ground");
	g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (LRG_NODE_VISUAL_TILEMAP);

	f->editor = lrg_editor_new ();
	lrg_node_set_visual (node, visual);
	lrg_editor_add_node (f->editor, node, NULL);
	f->node = node;
	f->painter = lrg_editor_tile_painter_new (f->editor);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->painter);
	g_clear_object (&f->editor);
}

static LrgNodeTilemap *
grid_of (LrgNode *node)
{
	return lrg_node_visual_get_tilemap (lrg_node_get_visual (node));
}

/* ==========================================================================
 * Model
 * ========================================================================== */

static void
test_model_basics (void)
{
	g_autoptr(LrgNodeTilemap) map = lrg_node_tilemap_new (4, 3);
	g_autoptr(LrgNodeTilemap) copy = NULL;
	gsize                     n = 0;
	const guint              *cells;

	g_assert_cmpuint (lrg_node_tilemap_get_width (map), ==, 4);
	g_assert_cmpuint (lrg_node_tilemap_get_height (map), ==, 3);
	g_assert_cmpuint (lrg_node_tilemap_get_n_layers (map), ==, 1);
	g_assert_cmpstr (lrg_node_tilemap_get_layer_name (map, 0), ==, "base");

	g_assert_true (lrg_node_tilemap_set_tile (map, 0, 3, 2, 7));
	g_assert_false (lrg_node_tilemap_set_tile (map, 0, 4, 0, 7));
	g_assert_false (lrg_node_tilemap_set_tile (map, 0, -1, 0, 7));
	g_assert_cmpuint (lrg_node_tilemap_get_tile (map, 0, 3, 2), ==, 7);
	g_assert_cmpuint (lrg_node_tilemap_get_tile (map, 0, 9, 9), ==, 0);
	g_assert_cmpuint (lrg_node_tilemap_get_tile (map, 5, 0, 0), ==, 0);

	cells = lrg_node_tilemap_get_layer_tiles (map, 0, &n);
	g_assert_cmpuint (n, ==, 12);
	g_assert_cmpuint (cells[2 * 4 + 3], ==, 7);

	/* Layers: add, find, can't remove the last one. */
	g_assert_cmpuint (lrg_node_tilemap_add_layer (map, "deco"), ==, 1);
	g_assert_cmpint (lrg_node_tilemap_find_layer (map, "deco"), ==, 1);
	g_assert_cmpint (lrg_node_tilemap_find_layer (map, "nope"), ==, -1);
	lrg_node_tilemap_set_layer_visible (map, 1, FALSE);
	g_assert_false (lrg_node_tilemap_get_layer_visible (map, 1));

	copy = lrg_node_tilemap_copy (map);
	g_assert_true (lrg_node_tilemap_equal (map, copy));
	lrg_node_tilemap_set_tile (copy, 1, 0, 0, 2);
	g_assert_false (lrg_node_tilemap_equal (map, copy));
	g_assert_true (lrg_node_tilemap_remove_layer (copy, 1));
	g_assert_false (lrg_node_tilemap_remove_layer (copy, 0));

	/* Resize keeps the overlapping top-left block. */
	g_assert_true (lrg_node_tilemap_resize (map, 2, 5));
	g_assert_cmpuint (lrg_node_tilemap_get_tile (map, 0, 1, 2), ==, 0);
	g_assert_true (lrg_node_tilemap_resize (map, 4, 3));
	g_assert_cmpuint (lrg_node_tilemap_get_tile (map, 0, 3, 2), ==, 0);
	g_assert_false (lrg_node_tilemap_resize (map, 0, 3));
}

static void
test_model_flood_region (void)
{
	g_autoptr(LrgNodeTilemap) map = lrg_node_tilemap_new (5, 5);
	g_autoptr(GArray)         region = NULL;
	gint                      y;

	/* A vertical wall at x = 2 splits the grid into two regions. */
	for (y = 0; y < 5; y++)
		lrg_node_tilemap_set_tile (map, 0, 2, y, 9);

	region = lrg_node_tilemap_flood_region (map, 0, 0, 0);
	g_assert_cmpuint (region->len, ==, 10);
	g_clear_pointer (&region, g_array_unref);

	region = lrg_node_tilemap_flood_region (map, 0, 2, 4);
	g_assert_cmpuint (region->len, ==, 5);
	g_clear_pointer (&region, g_array_unref);

	region = lrg_node_tilemap_flood_region (map, 0, 7, 7);
	g_assert_cmpuint (region->len, ==, 0);
}

static void
test_model_local_to_cell (void)
{
	g_autoptr(LrgNodeTilemap) map = lrg_node_tilemap_new (4, 4);
	gint                      x, y;

	/* Row 0 is the top row, just below the origin. */
	g_assert_true (lrg_node_tilemap_local_to_cell (map, 1.0f, 0.5f, -0.5f, &x, &y));
	g_assert_cmpint (x, ==, 0);
	g_assert_cmpint (y, ==, 0);
	g_assert_true (lrg_node_tilemap_local_to_cell (map, 2.0f, 7.9f, -7.9f, &x, &y));
	g_assert_cmpint (x, ==, 3);
	g_assert_cmpint (y, ==, 3);
	g_assert_false (lrg_node_tilemap_local_to_cell (map, 1.0f, -0.1f, -0.5f, &x, &y));
	g_assert_cmpint (x, ==, -1);
	g_assert_false (lrg_node_tilemap_local_to_cell (map, 1.0f, 0.5f, 0.5f, &x, &y));
}

/* ==========================================================================
 * Painter
 * ========================================================================== */

static void
test_painter_stroke_is_one_undo_step (Fixture       *f,
                                      gconstpointer  data)
{
	guint undo0 = lrg_editor_get_undo_count (f->editor);

	g_assert_null (grid_of (f->node));
	lrg_editor_tile_painter_set_tile (f->painter, 3);
	g_assert_true (lrg_editor_tile_painter_begin_stroke (f->painter, f->node, FALSE));
	g_assert_true (lrg_editor_tile_painter_is_stroking (f->painter));

	/* First paint created the default grid. */
	g_assert_nonnull (grid_of (f->node));
	g_assert_cmpuint (lrg_node_tilemap_get_width (grid_of (f->node)), ==, LRG_EDITOR_TILEMAP_DEFAULT_SIZE);

	/* A jump from (0,0) to (4,0) is joined by a line: 5 cells. */
	g_assert_cmpuint (lrg_editor_tile_painter_stroke_to (f->painter, 0, 0), ==, 1);
	g_assert_cmpuint (lrg_editor_tile_painter_stroke_to (f->painter, 4, 0), ==, 4);
	/* Re-painting the same cells changes nothing. */
	g_assert_cmpuint (lrg_editor_tile_painter_stroke_to (f->painter, 0, 0), ==, 0);
	g_assert_cmpuint (lrg_editor_tile_painter_end_stroke (f->painter), ==, 5);
	g_assert_false (lrg_editor_tile_painter_is_stroking (f->painter));

	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 1);
	g_assert_cmpuint (lrg_node_tilemap_count_tiles (grid_of (f->node), 0), ==, 5);
	g_assert_true (lrg_editor_is_dirty (f->editor));

	/* One undo removes the stroke and the grid it created. */
	lrg_editor_undo (f->editor);
	g_assert_null (grid_of (f->node));
	lrg_editor_redo (f->editor);
	g_assert_cmpuint (lrg_node_tilemap_count_tiles (grid_of (f->node), 0), ==, 5);
	g_assert_cmpuint (lrg_node_tilemap_get_tile (grid_of (f->node), 0, 2, 0), ==, 3);

	/* A second stroke (eraser, brush 3) is a separate step. */
	lrg_editor_tile_painter_set_brush_size (f->painter, 3);
	g_assert_true (lrg_editor_tile_painter_begin_stroke (f->painter, f->node, TRUE));
	lrg_editor_tile_painter_stroke_to (f->painter, 1, 1);
	g_assert_cmpuint (lrg_editor_tile_painter_end_stroke (f->painter), ==, 3);
	g_assert_cmpuint (lrg_node_tilemap_count_tiles (grid_of (f->node), 0), ==, 2);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_node_tilemap_count_tiles (grid_of (f->node), 0), ==, 5);
}

static void
test_painter_rect_fill_pick (Fixture       *f,
                             gconstpointer  data)
{
	LrgNodeTilemap *grid;
	guint           undo0;

	g_assert_nonnull (lrg_editor_tile_painter_ensure_tilemap (f->painter, f->node));
	grid = grid_of (f->node);
	undo0 = lrg_editor_get_undo_count (f->editor);

	/* Rectangle corners in any order, clipped to the grid. */
	lrg_editor_tile_painter_set_tile (f->painter, 5);
	g_assert_cmpuint (lrg_editor_tile_painter_paint_rect (f->painter, f->node, 3, 3, 1, 1, FALSE), ==, 9);
	g_assert_cmpuint (lrg_editor_tile_painter_paint_rect (f->painter, f->node, 14, 14, 40, 40, FALSE), ==, 4);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 2);

	/* Fill the empty background around the squares. */
	lrg_editor_tile_painter_set_tile (f->painter, 2);
	g_assert_cmpuint (lrg_editor_tile_painter_fill (f->painter, f->node, 0, 0, FALSE), ==, 256 - 13);
	g_assert_cmpuint (lrg_node_tilemap_get_tile (grid, 0, 2, 2), ==, 5);
	g_assert_cmpuint (lrg_node_tilemap_get_tile (grid, 0, 8, 8), ==, 2);

	/* Filling with the same id is a no-op (no history entry). */
	undo0 = lrg_editor_get_undo_count (f->editor);
	g_assert_cmpuint (lrg_editor_tile_painter_fill (f->painter, f->node, 0, 0, FALSE), ==, 0);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0);

	/* Eyedropper picks 5; empty cells leave the current tile alone. */
	g_assert_cmpuint (lrg_editor_tile_painter_pick (f->painter, f->node, 1, 1), ==, 5);
	g_assert_cmpuint (lrg_editor_tile_painter_get_tile (f->painter), ==, 5);
	lrg_editor_tile_painter_paint_rect (f->painter, f->node, 0, 0, 0, 0, TRUE);
	g_assert_cmpuint (lrg_editor_tile_painter_pick (f->painter, f->node, 0, 0), ==, 0);
	g_assert_cmpuint (lrg_editor_tile_painter_get_tile (f->painter), ==, 5);

	/* Undo the fill only. */
	lrg_editor_undo (f->editor);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_node_tilemap_get_tile (grid_of (f->node), 0, 8, 8), ==, 0);
	g_assert_cmpuint (lrg_node_tilemap_get_tile (grid_of (f->node), 0, 2, 2), ==, 5);
}

static void
test_painter_layers_and_cells (Fixture       *f,
                               gconstpointer  data)
{
	LrgNodeTilemap *grid = lrg_editor_tile_painter_ensure_tilemap (f->painter, f->node);
	const gint      cells[] = { 0, 0, 1, 0, 1, 0, 99, 99 };

	lrg_node_tilemap_add_layer (grid, "top");
	lrg_editor_tile_painter_set_layer (f->painter, 1);
	lrg_editor_tile_painter_set_tile (f->painter, 4);
	/* The duplicate and the out-of-range cell are skipped. */
	g_assert_cmpuint (lrg_editor_tile_painter_paint_cells (f->painter, f->node, cells, G_N_ELEMENTS (cells), FALSE),
	                  ==, 2);
	g_assert_cmpuint (lrg_node_tilemap_count_tiles (grid, 0), ==, 0);
	g_assert_cmpuint (lrg_node_tilemap_count_tiles (grid, 1), ==, 2);

	/* A layer index past the end clamps to the top layer. */
	lrg_editor_tile_painter_set_layer (f->painter, 7);
	g_assert_cmpuint (lrg_editor_tile_painter_paint_rect (f->painter, f->node, 5, 5, 5, 5, FALSE), ==, 1);
	g_assert_cmpuint (lrg_node_tilemap_count_tiles (grid, 1), ==, 3);
}

static void
test_painter_rejects_other_nodes (Fixture       *f,
                                  gconstpointer  data)
{
	g_autoptr(LrgNode) plain = lrg_node_new ("Group");

	lrg_editor_add_node (f->editor, plain, NULL);
	g_assert_false (lrg_editor_tile_painter_begin_stroke (f->painter, plain, FALSE));
	g_assert_null (lrg_editor_tile_painter_ensure_tilemap (f->painter, plain));
	g_assert_cmpuint (lrg_editor_tile_painter_fill (f->painter, plain, 0, 0, FALSE), ==, 0);
	g_assert_false (lrg_editor_tile_painter_is_stroking (f->painter));
}

static void
test_painter_cell_at (Fixture       *f,
                      gconstpointer  data)
{
	gfloat p[3];
	gint   x, y;

	lrg_editor_tile_painter_ensure_tilemap (f->painter, f->node);
	lrg_node_visual_set_param_double (lrg_node_get_visual (f->node), "tile_size", 2.0);
	lrg_node_set_location_xyz (f->node, 10.0f, 5.0f, 0.0f);

	p[0] = 10.0f + 2.0f * 3 + 0.5f;   /* column 3 */
	p[1] = 5.0f - 2.0f * 4 - 0.5f;    /* row 4 */
	p[2] = 0.0f;
	g_assert_true (lrg_editor_tile_painter_cell_at (f->node, p, &x, &y));
	g_assert_cmpint (x, ==, 3);
	g_assert_cmpint (y, ==, 4);

	p[0] = 9.0f;
	g_assert_false (lrg_editor_tile_painter_cell_at (f->node, p, &x, &y));
	g_assert_cmpint (x, ==, -1);
}

/* ==========================================================================
 * Serialization
 * ========================================================================== */

static void
test_serialize_round_trip (Fixture       *f,
                           gconstpointer  data)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           loaded = NULL;
	g_autoptr(GError)             error = NULL;
	g_autofree gchar             *yaml = NULL;
	LrgNodeTilemap               *grid;
	LrgNodeTilemap               *grid2;
	LrgNode                      *node2;

	grid = lrg_editor_tile_painter_ensure_tilemap (f->painter, f->node);
	lrg_node_tilemap_resize (grid, 3, 2);
	lrg_node_tilemap_set_tile_size (grid, 32, 24);
	lrg_node_tilemap_set_tile (grid, 0, 0, 0, 1);
	lrg_node_tilemap_set_tile (grid, 0, 2, 1, 12);
	lrg_node_tilemap_add_layer (grid, "deco");
	lrg_node_tilemap_set_tile (grid, 1, 1, 1, 4000000);
	lrg_node_tilemap_set_layer_visible (grid, 1, FALSE);

	yaml = lrg_level_serializer_save_to_data (ser, lrg_editor_get_level (f->editor), NULL);
	g_assert_nonnull (strstr (yaml, "tilemap:"));
	g_assert_nonnull (strstr (yaml, "0 0 12"));

	loaded = lrg_level_serializer_load_from_data (ser, yaml, -1, &error);
	g_assert_no_error (error);
	node2 = lrg_level_find_node (loaded, lrg_node_get_guid (f->node));
	g_assert_nonnull (node2);
	grid2 = grid_of (node2);
	g_assert_nonnull (grid2);
	g_assert_true (lrg_node_tilemap_equal (grid, grid2));
	g_assert_true (lrg_node_state_equal (f->node, node2));
}

static void
test_serialize_tolerant (void)
{
	g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
	g_autoptr(LrgLevel)           level = NULL;
	g_autoptr(GError)             error = NULL;
	LrgNodeTilemap               *grid;
	GPtrArray                    *top;
	const gchar                  *yaml =
		"type: level\n"
		"name: t\n"
		"nodes:\n"
		"  - name: map\n"
		"    guid: aaaa\n"
		"    visual:\n"
		"      kind: tilemap\n"
		"      tilemap:\n"
		"        width: 3\n"
		"        height: 2\n"
		"        layers:\n"
		"          - name: base\n"
		"            rows:\n"
		"              - \"1, 2, 3, 4, 5\"\n"
		"              - \"7\"\n"
		"              - \"9 9 9\"\n";

	level = lrg_level_serializer_load_from_data (ser, yaml, -1, &error);
	g_assert_no_error (error);
	top = lrg_node_get_children (lrg_level_get_root (level));
	grid = grid_of (g_ptr_array_index (top, 0));
	g_assert_nonnull (grid);
	/* Long rows are truncated, short rows padded, extra rows ignored. */
	g_assert_cmpuint (lrg_node_tilemap_get_tile (grid, 0, 2, 0), ==, 3);
	g_assert_cmpuint (lrg_node_tilemap_get_tile (grid, 0, 0, 1), ==, 7);
	g_assert_cmpuint (lrg_node_tilemap_get_tile (grid, 0, 1, 1), ==, 0);
	g_assert_cmpuint (lrg_node_tilemap_get_tile_width (grid), ==, 16);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor-tilemap/model/basics", test_model_basics);
	g_test_add_func ("/editor-tilemap/model/flood-region", test_model_flood_region);
	g_test_add_func ("/editor-tilemap/model/local-to-cell", test_model_local_to_cell);
#define ADD(path, fn) g_test_add (path, Fixture, NULL, fixture_setup, fn, fixture_teardown)
	ADD ("/editor-tilemap/painter/stroke-undo", test_painter_stroke_is_one_undo_step);
	ADD ("/editor-tilemap/painter/rect-fill-pick", test_painter_rect_fill_pick);
	ADD ("/editor-tilemap/painter/layers-cells", test_painter_layers_and_cells);
	ADD ("/editor-tilemap/painter/other-nodes", test_painter_rejects_other_nodes);
	ADD ("/editor-tilemap/painter/cell-at", test_painter_cell_at);
	ADD ("/editor-tilemap/serialize/round-trip", test_serialize_round_trip);
#undef ADD
	g_test_add_func ("/editor-tilemap/serialize/tolerant", test_serialize_tolerant);

	return g_test_run ();
}
