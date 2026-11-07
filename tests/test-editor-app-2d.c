/* test-editor-app-2d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The 0.9.0 2D tools of lrgeditor, driven through the canvas, the public
 * API and IPC on a headless app:
 *
 *   tiles/transform   X / Shift+X / Z flip and rotate the brush while painting;
 *                     a viewport stroke paints the transform bits; the
 *                     brush preview and chips follow
 *   tiles/rules       an .rtileset with an edges16 rule tile: the panel
 *                     lists it, painting autotiles (API, IPC and the
 *                     viewport gesture hooks), one undo step per gesture,
 *                     erase re-resolves the neighbours, Refresh all
 *   tiles/iso-hex     isometric and hexagonal grids: exact picking, cell
 *                     outlines, layout widgets, tiles.grid / tiles.cell_at
 *   tiles/viewport    a viewport drag with a rule tile brush is autotiled in
 *                     one undo step; isometric hover picks the diamond
 *   tiles/palette     the palette of an .rtileset (columns, badges), a
 *                     click picks a tile and keeps the transform, the
 *                     properties box follows the hovered tile
 *   tileset/edit      the .rtileset editor: create / open over IPC, the
 *                     sheet click, flags, data, animation, collision
 *                     (box, drag a point as one undo step, draw a
 *                     polygon), rule tiles (preset, 3x3 mask clicks,
 *                     outputs and weights, preview), save round trip
 *   sprite/edit       the .rsprite editor: grid and alpha slicing, a
 *                     manual rectangle, pivot drag, clips with frames,
 *                     durations, events, reorder, preview playback,
 *                     save round trip
 */

#include "editor-app-fixture.h"
#include <raylib.h>

#define NOMOD LRG_INPUT_MODIFIER_NONE

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static gchar *
project_file (EditorAppFixture *f,
              const gchar      *rel,
              const gchar      *text)
{
	g_autoptr(GError) error = NULL;
	gchar            *path;
	g_autofree gchar *dir = NULL;

	if (lrg_editor_app_get_project_dir (f->app) == NULL)
	{
		g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Demo", &error));
		g_assert_no_error (error);
	}
	path = g_build_filename (f->project, rel, NULL);
	dir = g_path_get_dirname (path);
	g_mkdir_with_parents (dir, 0755);
	if (text != NULL)
	{
		g_assert_true (g_file_set_contents (path, text, -1, &error));
		g_assert_no_error (error);
	}
	return path;
}

/* A transparent PNG with opaque rectangles (x, y, w, h quadruples). */
static gchar *
write_islands (EditorAppFixture *f,
               const gchar      *rel,
               gint              width,
               gint              height,
               const gint       *rects,
               guint             n_rects)
{
	gchar *path = project_file (f, rel, NULL);
	Image  img = GenImageColor (width, height, BLANK);
	guint  i;

	for (i = 0; i < n_rects; i++)
		ImageDrawRectangle (&img, rects[4 * i], rects[4 * i + 1], rects[4 * i + 2], rects[4 * i + 3], RED);
	g_assert_true (ExportImage (img, path));
	UnloadImage (img);
	return path;
}

static LrgWidget *
find (EditorAppFixture *f,
      const gchar      *name)
{
	LrgWidget *w = efx_find (f, name);

	if (w == NULL)
		g_error ("no widget named %s", name);
	return w;
}

/* Scrolls @name into view inside the scroll box @scroll. */
static void
reveal (EditorAppFixture *f,
        const gchar      *scroll,
        const gchar      *name)
{
	lrg_scroll_box_scroll_to_widget (LRG_SCROLL_BOX (find (f, scroll)), find (f, name));
	efx_frames (f, 40);   /* the scroll animates on a spring */
}

static LrgWidget *
present (EditorAppFixture *f,
         const gchar      *id)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *panel = lrg_dock_area_get_panel (dock, id);

	g_assert_nonnull (panel);
	lrg_dock_area_present_panel (dock, panel);
	efx_frames (f, 2);
	return lrg_dock_panel_get_child (panel);
}

static void
ipc_ok (EditorAppFixture *f,
        const gchar      *request)
{
	g_autoptr(JsonNode) r = efx_ipc_result (f, request);
}

static gboolean
ipc_fails (EditorAppFixture *f,
           const gchar      *request)
{
	g_autofree gchar *reply = efx_ipc (f, request);

	return strstr (reply, "\"error\"") != NULL;
}

/* A 8x8 tilemap node "Map" selected, with paint mode on. */
static LrgNode *
make_map (EditorAppFixture *f,
          guint             size)
{
	g_autofree gchar *req = NULL;

	ipc_ok (f, "{'id':1,'method':'node.create','params':{'name':'Map','kind':'tilemap'}}");
	req = g_strdup_printf ("{'id':2,'method':'tilemap.configure','params':{'node':'Map','width':%u,'height':%u}}",
	                       size, size);
	ipc_ok (f, req);
	ipc_ok (f, "{'id':3,'method':'selection.set','params':{'nodes':['Map']}}");
	efx_frames (f, 2);
	return lrg_editor_find_node (f->editor, "Map");
}

static guint
cell (LrgNode *node,
      guint    layer,
      gint     x,
      gint     y)
{
	return lrg_node_tilemap_get_tile (lrg_node_visual_get_tilemap (lrg_node_get_visual (node)), layer, x, y);
}

/* An .rtileset of 20 untextured 16x16 tiles: an edges16 rule tile "wall"
 * on tiles 0..15, a floor rule tile with weighted variants on 16..18,
 * tile 3 solid with a polygon, tile 17 with data, tile 19 animated. */
static gchar *
write_tileset (EditorAppFixture *f,
               const gchar      *rel)
{
	g_autofree gchar       *path = project_file (f, rel, NULL);
	g_autoptr(LrgTileset)   ts = lrg_tileset_new_empty (16, 16, 4, 20);
	g_autoptr(LrgRuleTile)  wall = lrg_rule_tile_new_edges16 ("wall", 1);
	g_autoptr(LrgRuleTile)  floor = lrg_rule_tile_new ("floor");
	g_autoptr(LrgTileRule)  any = lrg_tile_rule_new ();
	g_autoptr(GError)       error = NULL;
	const gfloat            ramp[6] = { 0, 16, 16, 0, 16, 16 };
	const guint             frames[2] = { 19, 18 };
	const guint             ms[2] = { 100, 200 };

	lrg_tileset_set_name (ts, "terrain");
	lrg_tileset_set_tile_properties (ts, 3, LRG_TILE_PROPERTY_SOLID);
	lrg_tileset_add_tile_collision_polygon (ts, 3, ramp, 6);
	lrg_tileset_set_tile_data (ts, 17, "sound", "stone");
	lrg_tileset_set_tile_animation (ts, 19, frames, ms, 2);
	lrg_tile_rule_add_output (any, 17, 1.0f);
	lrg_tile_rule_add_output (any, 18, 0.5f);
	lrg_rule_tile_add_rule (floor, any);
	lrg_tileset_add_rule_tile (ts, wall);
	lrg_tileset_add_rule_tile (ts, floor);
	g_assert_true (lrg_tileset_save_to_file (ts, path, &error));
	g_assert_no_error (error);
	return g_steal_pointer (&path);
}

static LrgEditorTilesPanel *
tiles_panel (EditorAppFixture *f)
{
	LrgEditorTilesPanel *tiles = lrg_editor_tiles_panel_find (f->app);

	g_assert_nonnull (tiles);
	return tiles;
}

/* ==========================================================================
 * Tiles panel
 * ========================================================================== */

static void
test_tiles_transform (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorTilesPanel *tiles = tiles_panel (f);
	LrgNode             *node;
	guint                value, undo0;
	gfloat               vx, vy, vw, vh, cx, cy;

	present (f, "viewport");
	node = make_map (f, 8);
	ipc_ok (f, "{'id':4,'method':'view.set','params':{'mode_2d':true}}");
	ipc_ok (f, "{'id':5,'method':'view.set','params':{'position':[4.5,-4.5,50],'target':[4.5,-4.5,0],"
	           "'ortho_height':20}}");
	efx_frames (f, 2);
	efx_key (f, GRL_KEY_B, NOMOD);
	efx_frames (f, 1);
	g_assert_true (lrg_editor_view_state_get_tile_mode (lrg_editor_app_get_view_state (f->app)));
	lrg_editor_tiles_panel_set_brush_tile (tiles, 3);

	/* X mirrors, Z turns clockwise, Shift+X mirrors vertically on top */
	efx_viewport_rect (f, &vx, &vy, &vw, &vh);
	cx = vx + vw * 0.5f;
	cy = vy + vh * 0.5f;
	efx_move (f, cx, cy);
	efx_key (f, GRL_KEY_X, NOMOD);
	value = lrg_editor_tiles_panel_get_brush_value (tiles);
	g_assert_cmpuint (lrg_tile_get_id (value), ==, 3);
	g_assert_true (lrg_tile_get_mirrored (value));
	g_assert_cmpuint (lrg_tile_get_rotation (value), ==, 0);
	efx_key (f, GRL_KEY_Z, NOMOD);
	value = lrg_editor_tiles_panel_get_brush_value (tiles);
	g_assert_true (value == lrg_tile_rotate (lrg_tile_make (3, TRUE, 0), 1));
	efx_key (f, GRL_KEY_X, LRG_INPUT_MODIFIER_SHIFT);
	value = lrg_editor_tiles_panel_get_brush_value (tiles);
	g_assert_true (value == lrg_tile_flip_vertically (lrg_tile_rotate (lrg_tile_make (3, TRUE, 0), 1)));
	efx_frames (f, 2);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':6,'method':'tiles.get'}");
		JsonObject         *brush = json_object_get_object_member (json_node_get_object (r), "brush");

		g_assert_nonnull (brush);
		g_assert_cmpint (json_object_get_int_member (brush, "id"), ==, 3);
		g_assert_cmpint (json_object_get_int_member (brush, "tile"), ==, (gint64) value);
	}

	/* A viewport click paints the value with its bits (one undo step) */
	undo0 = lrg_editor_get_undo_count (f->editor);
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_button (f, cx, cy, GRL_MOUSE_BUTTON_LEFT, FALSE);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 1);
	g_assert_cmpuint (cell (node, 0, 4, 4), ==, value);

	/* tiles.brush: absolute transform; reset; the keys need paint mode */
	ipc_ok (f, "{'id':7,'method':'tiles.brush','params':{'tile':5,'mirrored':false,'rotation':2}}");
	value = lrg_editor_tiles_panel_get_brush_value (tiles);
	g_assert_true (value == lrg_tile_make (5, FALSE, 2));
	lrg_editor_actions_activate (lrg_editor_app_get_actions (f->app), "tilemap.reset-transform");
	g_assert_cmpuint (lrg_editor_tiles_panel_get_brush_value (tiles), ==, 5);
	efx_key (f, GRL_KEY_B, NOMOD);
	efx_frames (f, 1);
	g_assert_false (lrg_editor_actions_is_enabled (lrg_editor_app_get_actions (f->app), "tilemap.flip-x"));
}

static void
test_tiles_rules (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorTilesPanel *tiles = tiles_panel (f);
	g_autofree gchar    *ts_path = write_tileset (f, "tiles/terrain.rtileset");
	g_autoptr(LrgEditorTilePainter) painter = NULL;
	LrgNode             *node;
	const gint           line[] = { 2, 3, 3, 3, 4, 3, 5, 3 };
	guint                undo0, n;
	g_autoptr(JsonNode)  r = NULL;

	present (f, "tiles");
	node = make_map (f, 8);
	g_assert_true (lrg_editor_tiles_panel_set_tileset (tiles, ts_path));
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_node_visual_get_asset (lrg_node_get_visual (node)), ==, "tiles/terrain.rtileset");
	g_assert_nonnull (lrg_editor_tiles_panel_get_tileset (tiles));
	g_assert_nonnull (efx_find (f, "tiles-rule-wall"));
	g_assert_nonnull (efx_find (f, "tiles-rule-floor"));

	/* Clicking the chip makes "wall" the brush */
	efx_click_widget (f, find (f, "tiles-rule-wall"));
	efx_frames (f, 1);
	g_assert_cmpstr (lrg_editor_tiles_panel_get_rule_tile (tiles), ==, "wall");

	/* A horizontal run: the ends see one neighbour, the middle two
	 * (edges16: tile 1 + N*1 + E*2 + S*4 + W*8) */
	undo0 = lrg_editor_get_undo_count (f->editor);
	n = lrg_editor_tiles_panel_paint_cells (tiles, line, G_N_ELEMENTS (line), FALSE);
	g_assert_cmpuint (n, ==, 4);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 1);
	g_assert_cmpuint (cell (node, 0, 2, 3), ==, 1 + 2);
	g_assert_cmpuint (cell (node, 0, 3, 3), ==, 1 + 2 + 8);
	g_assert_cmpuint (cell (node, 0, 5, 3), ==, 1 + 8);

	/* Erasing the end re-resolves its neighbour */
	n = lrg_editor_tiles_panel_paint_cells (tiles, line + 6, 2, TRUE);
	g_assert_cmpuint (cell (node, 0, 5, 3), ==, 0);
	g_assert_cmpuint (cell (node, 0, 4, 3), ==, 1 + 8);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (cell (node, 0, 5, 3), ==, 1 + 8);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (cell (node, 0, 2, 3), ==, 0);
	g_assert_cmpuint (cell (node, 0, 3, 3), ==, 0);

	/* The viewport's gesture hooks around a painter stroke: one undo
	 * step, autotiled on every sample */
	painter = lrg_editor_tile_painter_new (f->editor);
	lrg_editor_tile_painter_set_tile (painter, 1);   /* a tile the rule tile owns */
	undo0 = lrg_editor_get_undo_count (f->editor);
	lrg_editor_tiles_panel_begin_gesture (f->app, node);
	g_assert_true (lrg_editor_tile_painter_begin_stroke (painter, node, FALSE));
	lrg_editor_tile_painter_stroke_to (painter, 1, 1);
	lrg_editor_tiles_panel_update_gesture (f->app);
	g_assert_cmpuint (cell (node, 0, 1, 1), ==, 1);   /* isolated */
	lrg_editor_tile_painter_stroke_to (painter, 1, 3);
	lrg_editor_tiles_panel_update_gesture (f->app);
	lrg_editor_tile_painter_end_stroke (painter);
	lrg_editor_tiles_panel_end_gesture (f->app);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 1);
	g_assert_cmpuint (cell (node, 0, 1, 1), ==, 1 + 4);       /* S */
	g_assert_cmpuint (cell (node, 0, 1, 2), ==, 1 + 1 + 4);   /* N + S */
	g_assert_cmpuint (cell (node, 0, 1, 3), ==, 1 + 1);       /* N */
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (cell (node, 0, 1, 2), ==, 0);

	/* IPC with an explicit rule tile; weighted floor variants are owned */
	r = efx_ipc_result (f, "{'id':8,'method':'tiles.paint','params':{'cells':[0,0,1,0,2,0,3,0],'rule_tile':'floor'}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "changed"), ==, 4);
	g_clear_pointer (&r, json_node_unref);
	g_assert_true (cell (node, 0, 0, 0) == 17 || cell (node, 0, 0, 0) == 18);
	g_assert_true (ipc_fails (f, "{'id':9,'method':'tiles.paint','params':{'cells':[0,0],'rule_tile':'nope'}}"));

	/* Plain tiles of the wall range, then Refresh all resolves them */
	ipc_ok (f, "{'id':10,'method':'tiles.brush','params':{'rule_tile':null,'tile':1}}");
	g_assert_null (lrg_editor_tiles_panel_get_rule_tile (tiles));
	ipc_ok (f, "{'id':11,'method':'tiles.paint','params':{'cells':[6,6,7,6]}}");
	g_assert_cmpuint (cell (node, 0, 6, 6), ==, 1);
	undo0 = lrg_editor_get_undo_count (f->editor);
	r = efx_ipc_result (f, "{'id':12,'method':'tiles.refresh'}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "changed"), ==, 2);
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 1);
	g_assert_cmpuint (cell (node, 0, 6, 6), ==, 1 + 2);
	g_assert_cmpuint (cell (node, 0, 7, 6), ==, 1 + 8);

	/* tiles.get lists the rule tiles and the brush tile's properties */
	ipc_ok (f, "{'id':13,'method':'tiles.brush','params':{'tile':4}}");
	r = efx_ipc_result (f, "{'id':14,'method':'tiles.get'}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "tileset_kind"), ==, "rtileset");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "rule_tiles")), ==, 2);
	{
		JsonObject *tile = json_object_get_object_member (json_node_get_object (r), "tile");

		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (tile, "collision")), ==, 1);
		g_assert_cmpstr (json_object_get_string_member (tile, "rule_tile"), ==, "wall");
	}
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':15,'method':'tiles.tile_info','params':{'tile':18}}");
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (json_node_get_object (r), "data"),
	                                                "sound"), ==, "stone");
}

static void
test_tiles_iso_hex (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgEditorTilesPanel *tiles = tiles_panel (f);
	LrgNode             *node;
	LrgTilemap          *map;
	gfloat               s, wx, wy, poly[12];
	gint                 x, y;
	guint                undo0;
	g_autoptr(JsonNode)  r = NULL;

	present (f, "tiles");
	node = make_map (f, 10);
	lrg_node_tilemap_set_tile_size (lrg_node_visual_get_tilemap (lrg_node_get_visual (node)), 64, 64);

	/* The layout combo makes it isometric (one undo step) */
	undo0 = lrg_editor_get_undo_count (f->editor);
	g_assert_true (lrg_editor_tiles_panel_set_grid (tiles, LRG_TILEMAP_ORIENTATION_ISOMETRIC, LRG_TILEMAP_STAGGER_AXIS_Y,
	                                                LRG_TILEMAP_STAGGER_INDEX_ODD, 0, 64, 32));
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 1);
	efx_frames (f, 2);
	g_assert_true (lrg_widget_get_visible (find (f, "tiles-cell-row")));
	g_assert_cmpint (lrg_combo_box_get_selected (LRG_COMBO_BOX (find (f, "tiles-orientation"))), ==, 1);

	/* Cell centres pick back exactly, also near the diamond's corners */
	map = lrg_editor_tiles_panel_get_geometry (node, &s);
	g_assert_nonnull (map);
	g_assert_cmpfloat_with_epsilon (s, 1.0f / 64.0f, 1e-6);
	lrg_tilemap_cell_to_world (map, 3, 4, &wx, &wy);
	g_assert_true (lrg_editor_tiles_panel_local_to_cell (node, wx * s, -wy * s, &x, &y));
	g_assert_cmpint (x, ==, 3);
	g_assert_cmpint (y, ==, 4);
	/* just inside the top corner of the diamond (a bounding box pick would
	 * be ambiguous there) */
	g_assert_true (lrg_editor_tiles_panel_local_to_cell (node, wx * s, -(wy - 14.0f) * s, &x, &y));
	g_assert_cmpint (x, ==, 3);
	g_assert_cmpint (y, ==, 4);
	g_assert_cmpuint (lrg_editor_tiles_panel_get_cell_polygon (node, 3, 4, poly), ==, 4);

	/* tiles.cell_at answers the same */
	r = efx_ipc_result (f, "{'id':4,'method':'tiles.get'}");
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (json_node_get_object (r), "grid"),
	                                                "orientation"), ==, "isometric");
	g_clear_pointer (&r, json_node_unref);

	/* Hexagonal over IPC: six-point outlines, hex neighbour layout */
	ipc_ok (f, "{'id':5,'method':'tiles.grid','params':{'orientation':'hexagonal','stagger_axis':'y',"
	           "'stagger_index':'odd','hex_side':32,'cell_size':[64,56]}}");
	efx_frames (f, 2);
	g_assert_true (lrg_widget_get_visible (find (f, "tiles-hex-row")));
	g_assert_cmpuint (lrg_editor_tiles_panel_get_cell_polygon (node, 2, 2, poly), ==, 6);
	map = lrg_editor_tiles_panel_get_geometry (node, &s);
	lrg_tilemap_cell_to_world (map, 2, 3, &wx, &wy);
	{
		g_autofree gchar *req = g_strdup_printf ("{'id':6,'method':'tiles.cell_at','params':{'local':[%.6f,%.6f]}}",
		                                         wx * s, -wy * s);

		r = efx_ipc_result (f, req);
	}
	g_assert_cmpint (json_array_get_int_element (json_object_get_array_member (json_node_get_object (r), "cell"), 0), ==, 2);
	g_assert_cmpint (json_array_get_int_element (json_object_get_array_member (json_node_get_object (r), "cell"), 1), ==, 3);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "polygon")), ==, 6);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':7,'method':'tiles.get'}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "layout"), ==, "hex-rows-odd");
	g_clear_pointer (&r, json_node_unref);
	g_assert_true (ipc_fails (f, "{'id':8,'method':'tiles.grid','params':{'orientation':'round'}}"));

	/* Undo goes back to isometric, then orthogonal */
	lrg_editor_undo (f->editor);
	lrg_editor_undo (f->editor);
	efx_frames (f, 2);
	g_assert_false (lrg_widget_get_visible (find (f, "tiles-cell-row")));
	g_assert_cmpuint (lrg_editor_tiles_panel_get_cell_polygon (node, 0, 0, poly), ==, 4);
}

/* The viewport wraps its tile tools in the panel's gesture hooks: a drag
 * with a rule tile brush is autotiled and one undo step; isometric grids
 * pick the diamond under the pointer. */
static void
test_tiles_viewport (EditorAppFixture *f,
                     gconstpointer     data)
{
	LrgEditorTilesPanel *tiles = tiles_panel (f);
	g_autofree gchar    *ts_path = write_tileset (f, "tiles/terrain.rtileset");
	LrgNode             *node;
	LrgTilemap          *map;
	g_autoptr(JsonNode)  r = NULL;
	gfloat               vx, vy, vw, vh, cx, cy, px, s, wx, wy;
	guint                undo0;
	gint                 i;

	present (f, "viewport");
	node = make_map (f, 8);
	g_assert_true (lrg_editor_tiles_panel_set_tileset (tiles, ts_path));
	g_assert_true (lrg_editor_tiles_panel_set_rule_tile (tiles, "wall"));
	ipc_ok (f, "{'id':4,'method':'view.set','params':{'mode_2d':true}}");
	ipc_ok (f, "{'id':5,'method':'view.set','params':{'position':[4.5,-4.5,50],'target':[4.5,-4.5,0],"
	           "'ortho_height':20}}");
	efx_frames (f, 2);
	efx_key (f, GRL_KEY_B, NOMOD);
	efx_frames (f, 1);
	efx_viewport_rect (f, &vx, &vy, &vw, &vh);
	cx = vx + vw * 0.5f;
	cy = vy + vh * 0.5f;
	px = vh / 20.0f;

	/* Drag from cell (2, 4) to (5, 4): one undo step, edges joined */
	undo0 = lrg_editor_get_undo_count (f->editor);
	efx_button (f, cx - 2.0f * px, cy, GRL_MOUSE_BUTTON_LEFT, TRUE);
	for (i = 1; i <= 6; i++)
		efx_button (f, cx - 2.0f * px + px * 0.5f * (gfloat) i, cy, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_button (f, cx + px, cy, GRL_MOUSE_BUTTON_LEFT, FALSE);
	g_assert_cmpuint (lrg_editor_get_undo_count (f->editor), ==, undo0 + 1);
	g_assert_cmpuint (cell (node, 0, 2, 4), ==, 1 + 2);
	g_assert_cmpuint (cell (node, 0, 3, 4), ==, 1 + 2 + 8);
	g_assert_cmpuint (cell (node, 0, 4, 4), ==, 1 + 2 + 8);
	g_assert_cmpuint (cell (node, 0, 5, 4), ==, 1 + 8);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (cell (node, 0, 3, 4), ==, 0);

	/* Isometric: hovering the centre of diamond (3, 4) reports it */
	g_assert_true (lrg_editor_tiles_panel_set_grid (tiles, LRG_TILEMAP_ORIENTATION_ISOMETRIC, LRG_TILEMAP_STAGGER_AXIS_Y,
	                                                LRG_TILEMAP_STAGGER_INDEX_ODD, 0, 16, 8));
	efx_frames (f, 2);
	map = lrg_editor_tiles_panel_get_geometry (node, &s);
	lrg_tilemap_cell_to_world (map, 3, 4, &wx, &wy);
	efx_move (f, cx + (wx * s - 4.5f) * px, cy - (-wy * s + 4.5f) * px);
	efx_move (f, cx + (wx * s - 4.5f) * px, cy - (-wy * s + 4.5f) * px);
	r = efx_ipc_result (f, "{'id':6,'method':'tilemap.tool'}");
	g_assert_cmpint (json_array_get_int_element (json_object_get_array_member (json_node_get_object (r), "hover"), 0), ==, 3);
	g_assert_cmpint (json_array_get_int_element (json_object_get_array_member (json_node_get_object (r), "hover"), 1), ==, 4);
}

static void
test_tiles_palette (EditorAppFixture *f,
                    gconstpointer     data)
{
	LrgEditorTilesPanel *tiles = tiles_panel (f);
	g_autofree gchar    *ts_path = write_tileset (f, "tiles/terrain.rtileset");
	guint                cols, rows;
	gfloat               x, y, w, h, cw;

	present (f, "tiles");
	make_map (f, 8);
	g_assert_true (lrg_editor_tiles_panel_set_tileset (tiles, ts_path));
	efx_frames (f, 3);
	lrg_editor_tiles_panel_get_palette_size (tiles, &cols, &rows);
	g_assert_cmpuint (cols, ==, 4);
	g_assert_cmpuint (rows, ==, 5);

	/* Keep a transform, then click tile 18 (index 17: row 4, column 1) */
	ipc_ok (f, "{'id':4,'method':'tiles.brush','params':{'tile':2,'mirrored':true,'rotation':1}}");
	reveal (f, "tiles-scroll", "tiles-tile-info");
	reveal (f, "tiles-scroll", "tiles-palette");
	efx_rect_of (find (f, "tiles-palette"), &x, &y, &w, &h);
	cw = MIN (w / (gfloat) cols, 64.0f);   /* palette cells are at most 64 px */
	efx_move (f, x + cw * 1.5f, y + cw * 4.5f);
	g_assert_cmpuint (lrg_editor_tiles_panel_get_info_tile (tiles), ==, 18);
	efx_button (f, x + cw * 1.5f, y + cw * 4.5f, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_button (f, x + cw * 1.5f, y + cw * 4.5f, GRL_MOUSE_BUTTON_LEFT, FALSE);
	g_assert_cmpuint (lrg_editor_tiles_panel_get_brush_value (tiles), ==, lrg_tile_make (18, TRUE, 1));
	efx_move (f, x - 40.0f, y - 40.0f);
	g_assert_cmpuint (lrg_editor_tiles_panel_get_info_tile (tiles), ==, 18);
	g_assert_cmpfloat (lrg_widget_get_height (find (f, "tiles-tile-info")), >, 40.0f);

	/* A plain image tileset clears the rule tiles section */
	reveal (f, "tiles-scroll", "tiles-rule-wall");
	efx_click_widget (f, find (f, "tiles-rule-wall"));
	g_assert_cmpstr (lrg_editor_tiles_panel_get_rule_tile (tiles), ==, "wall");
	g_assert_true (lrg_editor_tiles_panel_set_tileset (tiles, "tiles/none.png"));
	efx_frames (f, 2);
	g_assert_null (lrg_editor_tiles_panel_get_rule_tile (tiles));
	g_assert_null (efx_find (f, "tiles-rule-wall"));
	lrg_editor_undo (f->editor);
	efx_frames (f, 2);
	g_assert_nonnull (efx_find (f, "tiles-rule-wall"));
}

/* ==========================================================================
 * The .rtileset editor
 * ========================================================================== */

/* A canvas point over tile @index of the sheet. */
static void
sheet_point (EditorAppFixture       *f,
             LrgEditorTilesetEditor *ed,
             gint                    index,
             gfloat                 *px,
             gfloat                 *py)
{
	LrgWidget *sheet = find (f, "tileset-sheet");
	gfloat     x, y, w, h, i, j;

	efx_rect_of (sheet, &x, &y, &w, &h);
	for (j = y + 2.0f; j < y + h; j += 3.0f)
		for (i = x + 2.0f; i < x + w; i += 3.0f)
			if (lrg_editor_tileset_editor_tile_at (ed, i, j) == index &&
			    lrg_editor_tileset_editor_tile_at (ed, i + 4.0f, j + 4.0f) == index)
			{
				*px = i + 2.0f;
				*py = j + 2.0f;
				return;
			}
	g_error ("tile %d not on the sheet", index);
}

static void
test_tileset_edit (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorTilesetEditor *ed;
	LrgTileset             *ts;
	LrgEditorDocument      *doc;
	g_autofree gchar       *path = project_file (f, "tiles/cave.rtileset", NULL);
	g_autofree gchar       *req = NULL;
	g_autoptr(JsonNode)     r = NULL;
	g_autoptr(GError)       error = NULL;
	g_autoptr(LrgTileset)   saved = NULL;
	gfloat                  px, py, x, y, w, h, side, s, ox, oy;
	guint                   n;
	const gfloat           *pts;
	LrgRuleTile            *rt;

	/* tileset.create + tileset.open */
	g_unlink (path);
	ipc_ok (f, "{'id':1,'method':'tileset.create','params':{'path':'tiles/cave.rtileset','tile_size':[16,16],"
	           "'name':'cave'}}");
	g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));
	g_assert_true (ipc_fails (f, "{'id':2,'method':'tileset.create','params':{'path':'tiles/cave.rtileset'}}"));
	req = g_strdup_printf ("{'id':3,'method':'tileset.open','params':{'path':'%s'}}", path);
	ipc_ok (f, req);
	efx_frames (f, 3);
	ed = lrg_editor_tileset_editor_open (f->app, path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (ed);
	ts = lrg_editor_tileset_editor_get_tileset (ed);
	g_assert_nonnull (ts);
	g_assert_cmpuint (lrg_tileset_get_tile_count (ts), ==, 64);
	doc = lrg_editor_data_editor_get_document (LRG_EDITOR_DATA_EDITOR (ed));

	/* Clicking a tile of the sheet selects it */
	sheet_point (f, ed, 9, &px, &py);
	efx_click (f, px, py);
	g_assert_cmpint (lrg_editor_tileset_editor_get_selected_tile (ed), ==, 9);

	/* Flags through the chip, data and animation over IPC */
	reveal (f, "tileset-scroll", "tileset-flag-Solid");
	efx_click_widget (f, find (f, "tileset-flag-Solid"));
	g_assert_true (lrg_tileset_get_tile_properties (lrg_editor_tileset_editor_get_tileset (ed), 9) & LRG_TILE_PROPERTY_SOLID);
	r = efx_ipc_result (f, "{'id':4,'method':'tileset.tile','params':{'tile':9,'data':{'kind':'lava','damage':3},"
	                   "'animation':[{'tile':9,'duration':120},{'tile':10,'duration':80}]}}");
	g_clear_pointer (&r, json_node_unref);
	ts = lrg_editor_tileset_editor_get_tileset (ed);
	g_assert_cmpstr (lrg_tileset_get_tile_data (ts, 9, "damage"), ==, "3");
	g_assert_cmpuint (lrg_tileset_get_tile_animation_length (ts, 9), ==, 2);
	efx_frames (f, 2);
	g_assert_nonnull (efx_find (f, "tileset-data-remove-kind"));
	g_assert_nonnull (efx_find (f, "tileset-frame-1"));
	reveal (f, "tileset-scroll", "tileset-data-remove-kind");
	efx_click_widget (f, find (f, "tileset-data-remove-kind"));
	g_assert_null (lrg_tileset_get_tile_data (lrg_editor_tileset_editor_get_tileset (ed), 9, "kind"));
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	g_assert_cmpstr (lrg_tileset_get_tile_data (lrg_editor_tileset_editor_get_tileset (ed), 9, "kind"), ==, "lava");

	/* Collision: a box, then drag its first corner to (4, 4): one step */
	reveal (f, "tileset-scroll", "tileset-collision-box");
	efx_click_widget (f, find (f, "tileset-collision-box"));
	ts = lrg_editor_tileset_editor_get_tileset (ed);
	g_assert_cmpuint (lrg_tileset_get_tile_collision_polygon_count (ts, 9), ==, 1);
	efx_frames (f, 1);
	reveal (f, "tileset-scroll", "tileset-collision");
	efx_rect_of (find (f, "tileset-collision"), &x, &y, &w, &h);
	side = MIN (w, h) - 16.0f;
	s = MAX (1.0f, side / 16.0f);
	ox = x + (w - 16.0f * s) * 0.5f;
	oy = y + (h - 16.0f * s) * 0.5f;
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, ox, oy, ox + 4.0f * s, oy + 4.0f * s, 4);
	ts = lrg_editor_tileset_editor_get_tileset (ed);
	pts = lrg_tileset_get_tile_collision_polygon (ts, 9, 0, &n);
	g_assert_cmpuint (n, ==, 8);
	g_assert_cmpfloat (pts[0], ==, 4.0f);
	g_assert_cmpfloat (pts[1], ==, 4.0f);
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	pts = lrg_tileset_get_tile_collision_polygon (lrg_editor_tileset_editor_get_tileset (ed), 9, 0, &n);
	g_assert_cmpfloat (pts[0], ==, 0.0f);

	/* Draw a triangle: three clicks and a click on the first point */
	efx_click_widget (f, find (f, "tileset-collision-draw"));
	efx_click (f, ox + 2.0f * s, oy + 14.0f * s);
	efx_click (f, ox + 14.0f * s, oy + 14.0f * s);
	efx_click (f, ox + 8.0f * s, oy + 2.0f * s);
	efx_click (f, ox + 2.0f * s, oy + 14.0f * s);
	g_assert_cmpuint (lrg_tileset_get_tile_collision_polygon_count (lrg_editor_tileset_editor_get_tileset (ed), 9), ==, 2);
	ipc_ok (f, "{'id':5,'method':'tileset.collision','params':{'tile':9,'op':'remove_point','index':1,'point':0}}");
	g_assert_cmpuint (lrg_tileset_get_tile_collision_polygon_count (lrg_editor_tileset_editor_get_tileset (ed), 9), ==, 1);

	/* Rule tiles: a 16-tile preset from tile 16 */
	r = efx_ipc_result (f, "{'id':6,'method':'tileset.rule_tile','params':{'op':'preset','name':'rock',"
	                   "'preset':'edges16','first':16}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "rules")), ==, 16);
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpstr (lrg_editor_tileset_editor_get_selected_rule_tile (ed), ==, "rock");
	efx_frames (f, 2);
	g_assert_true (lrg_widget_get_visible (find (f, "tileset-page-rules-box")));
	g_assert_false (lrg_widget_get_visible (find (f, "tileset-page-tile-box")));
	g_assert_true (ipc_fails (f, "{'id':7,'method':'tileset.rule_tile','params':{'op':'preset','name':'rock2',"
	                          "'preset':'blob47','first':40}}"));
	efx_frames (f, 3);
	g_assert_nonnull (efx_find (f, "tileset-rule-mask-15"));

	/* A new empty rule tile and a rule; the mask's N cell cycles */
	reveal (f, "tileset-scroll", "tileset-rule-tile-name");
	efx_click_widget (f, find (f, "tileset-rule-tile-name"));
	efx_type (f, "moss");
	efx_click_widget (f, find (f, "tileset-rule-tile-new"));
	g_assert_cmpstr (lrg_editor_tileset_editor_get_selected_rule_tile (ed), ==, "moss");
	efx_click (f, px, py);   /* the sheet's tile 9 */
	reveal (f, "tileset-scroll", "tileset-rule-add");
	efx_click_widget (f, find (f, "tileset-rule-add"));
	efx_frames (f, 3);
	rt = lrg_tileset_get_rule_tile_by_name (lrg_editor_tileset_editor_get_tileset (ed), "moss");
	g_assert_cmpuint (lrg_rule_tile_get_n_rules (rt), ==, 1);
	g_assert_cmpuint (lrg_tile_rule_get_output (lrg_rule_tile_get_rule (rt, 0), 0, NULL), ==, 10);
	reveal (f, "tileset-scroll", "tileset-rule-mask-0");
	efx_rect_of (find (f, "tileset-rule-mask-0"), &x, &y, &w, &h);
	efx_click (f, x + MIN (w, h) * 0.5f, y + MIN (w, h) / 6.0f);
	rt = lrg_tileset_get_rule_tile_by_name (lrg_editor_tileset_editor_get_tileset (ed), "moss");
	g_assert_cmpint (lrg_tile_rule_get_neighbor (lrg_rule_tile_get_rule (rt, 0), LRG_TILE_NEIGHBOR_N), ==,
	                 LRG_TILE_RULE_MATCH_THIS);
	efx_click (f, x + MIN (w, h) * 0.5f, y + MIN (w, h) / 6.0f);
	rt = lrg_tileset_get_rule_tile_by_name (lrg_editor_tileset_editor_get_tileset (ed), "moss");
	g_assert_cmpint (lrg_tile_rule_get_neighbor (lrg_rule_tile_get_rule (rt, 0), LRG_TILE_NEIGHBOR_N), ==,
	                 LRG_TILE_RULE_MATCH_NOT_THIS);

	/* Outputs with weights, a pattern and a transform over IPC */
	r = efx_ipc_result (f, "{'id':8,'method':'tileset.rule','params':{'rule_tile':'moss','op':'set','index':0,"
	                   "'pattern':['.#.','-@-','...'],'transform':'rotated',"
	                   "'tiles':[11,{'tile':12,'weight':0.25,'rotate':1}]}}");
	{
		JsonObject *rule = json_array_get_object_element (json_object_get_array_member (json_node_get_object (r), "rules"), 0);
		JsonArray  *pattern = json_object_get_array_member (rule, "pattern");

		g_assert_cmpstr (json_array_get_string_element (pattern, 0), ==, ".#.");
		g_assert_cmpstr (json_array_get_string_element (pattern, 1), ==, "-@-");
		g_assert_cmpstr (json_object_get_string_member (rule, "transform"), ==, "rotated");
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (rule, "tiles")), ==, 2);
	}
	g_clear_pointer (&r, json_node_unref);

	/* The preview resolves a scratch map without editing */
	n = lrg_editor_document_can_undo (doc) ? 1 : 0;
	r = efx_ipc_result (f, "{'id':9,'method':'tileset.preview','params':{'rule_tile':'rock','cells':[1,1,2,1],"
	                   "'size':[4,4]}}");
	g_assert_cmpint (json_array_get_int_element (json_object_get_array_member (json_node_get_object (r), "tiles"), 5), ==,
	                 17 + 2);
	g_clear_pointer (&r, json_node_unref);
	g_assert_cmpuint (n, ==, 1);

	/* Save and load with the engine */
	ipc_ok (f, "{'id':10,'method':'tileset.save'}");
	g_assert_false (lrg_editor_data_editor_get_dirty (LRG_EDITOR_DATA_EDITOR (ed)));
	saved = lrg_tileset_new_from_asset_file (path, FALSE, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_tileset_get_tile_properties (saved, 9) & LRG_TILE_PROPERTY_SOLID);
	g_assert_cmpstr (lrg_tileset_get_tile_data (saved, 9, "kind"), ==, "lava");
	g_assert_cmpuint (lrg_tileset_get_n_rule_tiles (saved), ==, 2);
	g_assert_nonnull (lrg_tileset_get_rule_tile_by_name (saved, "moss"));
	r = efx_ipc_result (f, "{'id':11,'method':'tileset.get'}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "rule_tiles")), ==, 2);
}

/* ==========================================================================
 * The .rsprite editor
 * ========================================================================== */

static void
test_sprite_edit (EditorAppFixture *f,
                  gconstpointer     data)
{
	static const gint       islands[] = { 4, 4, 20, 24, 40, 6, 16, 22, 70, 2, 24, 28 };
	LrgEditorSpriteEditor  *ed;
	LrgSpriteSheet         *sheet;
	LrgSpriteAnimation     *clip;
	g_autofree gchar       *png = write_islands (f, "art/hero.png", 128, 32, islands, 3);
	g_autofree gchar       *path = NULL;
	g_autoptr(GError)       error = NULL;
	g_autoptr(JsonNode)     r = NULL;
	g_autoptr(LrgSpriteSheet) saved = NULL;
	gfloat                  cx, cy, ex, ey;
	guint                   pos;
	LrgAtlasRegion         *frame;

	/* sprite.create with a frame size slices a grid over the image */
	ipc_ok (f, "{'id':1,'method':'sprite.create','params':{'path':'art/hero.rsprite','image':'art/hero.png',"
	           "'frame_size':[32,32]}}");
	path = g_build_filename (f->project, "art/hero.rsprite", NULL);
	ed = lrg_editor_sprite_editor_open (f->app, path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (ed);
	efx_frames (f, 3);
	sheet = lrg_editor_sprite_editor_get_sheet (ed);
	g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (sheet), ==, 4);

	/* Grid re-slice with a pivot (one undo step) */
	r = efx_ipc_result (f, "{'id':2,'method':'sprite.slice','params':{'mode':'grid','frame_size':[16,16],"
	                   "'pivot':[8,15]}}");
	g_clear_pointer (&r, json_node_unref);
	sheet = lrg_editor_sprite_editor_get_sheet (ed);
	g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (sheet), ==, 16);
	g_assert_cmpfloat (lrg_atlas_region_get_pivot_y (lrg_sprite_sheet_get_frame (sheet, 3)), ==, 15.0f);
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (lrg_editor_sprite_editor_get_sheet (ed)), ==, 4);

	/* Auto slicing finds the three islands */
	efx_click_widget (f, find (f, "sprite-mode-Auto"));
	efx_frames (f, 2);
	efx_click_widget (f, find (f, "sprite-slice-alpha"));
	sheet = lrg_editor_sprite_editor_get_sheet (ed);
	g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (sheet), ==, 3);
	frame = lrg_sprite_sheet_get_frame (sheet, 0);
	g_assert_cmpint (lrg_atlas_region_get_width (frame), ==, 20);
	g_assert_cmpfloat (lrg_atlas_region_get_pivot_y (frame), ==, 24.0f);   /* pivot (0.5, 1) of 20 x 24 */

	/* Manual: drag a rectangle on the image */
	efx_click_widget (f, find (f, "sprite-mode-Manual"));
	efx_frames (f, 2);
	lrg_editor_sprite_editor_image_to_canvas (ed, 100.0f, 2.0f, &cx, &cy);
	lrg_editor_sprite_editor_image_to_canvas (ed, 124.0f, 30.0f, &ex, &ey);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, cx, cy, ex, ey, 4);
	sheet = lrg_editor_sprite_editor_get_sheet (ed);
	g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (sheet), ==, 4);
	g_assert_cmpint (lrg_editor_sprite_editor_get_selected_frame (ed), ==, 3);
	frame = lrg_sprite_sheet_get_frame (sheet, 3);
	g_assert_cmpint (lrg_atlas_region_get_x (frame), ==, 100);
	g_assert_cmpint (lrg_atlas_region_get_width (frame), ==, 24);

	/* Drag its pivot (at its bottom centre) up by 4 px: one undo step */
	lrg_editor_sprite_editor_image_to_canvas (ed, 100.0f + lrg_atlas_region_get_pivot_x (frame),
	                                          2.0f + lrg_atlas_region_get_pivot_y (frame), &cx, &cy);
	lrg_editor_sprite_editor_image_to_canvas (ed, 100.0f + lrg_atlas_region_get_pivot_x (frame),
	                                          2.0f + lrg_atlas_region_get_pivot_y (frame) - 4.0f, &ex, &ey);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, cx, cy, ex, ey, 3);
	frame = lrg_sprite_sheet_get_frame (lrg_editor_sprite_editor_get_sheet (ed), 3);
	g_assert_cmpfloat (lrg_atlas_region_get_pivot_y (frame), ==, 24.0f);
	g_assert_true (lrg_editor_data_editor_undo (LRG_EDITOR_DATA_EDITOR (ed)));
	frame = lrg_sprite_sheet_get_frame (lrg_editor_sprite_editor_get_sheet (ed), 3);
	g_assert_cmpfloat (lrg_atlas_region_get_pivot_y (frame), ==, 28.0f);

	/* Name a frame; clips with frames by index and name; durations */
	ipc_ok (f, "{'id':3,'method':'sprite.frame','params':{'op':'set','index':3,'name':'sword'}}");
	r = efx_ipc_result (f, "{'id':4,'method':'sprite.clip','params':{'op':'add','name':'run','fps':10,"
	                   "'frames':[0,1,{'frame':'sword','duration':0.25}]}}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "frames")), ==, 3);
	g_clear_pointer (&r, json_node_unref);
	clip = lrg_sprite_sheet_get_clip (lrg_editor_sprite_editor_get_sheet (ed), "run");
	g_assert_nonnull (clip);
	g_assert_cmpfloat_with_epsilon (lrg_sprite_animation_get_duration (clip), 0.45f, 1e-4);
	g_assert_cmpstr (lrg_editor_sprite_editor_get_selected_clip (ed), ==, "run");

	/* Events stay with their frame when the frame moves */
	ipc_ok (f, "{'id':5,'method':'sprite.event','params':{'clip':'run','op':'add','frame':1,'name':'footstep'}}");
	ipc_ok (f, "{'id':6,'method':'sprite.clip_frame','params':{'clip':'run','op':'move','index':1,'to':0}}");
	clip = lrg_sprite_sheet_get_clip (lrg_editor_sprite_editor_get_sheet (ed), "run");
	g_assert_cmpuint (lrg_sprite_animation_get_frame (clip, 0, NULL), ==, 1);
	{
		guint frame_pos = 99;

		g_assert_cmpstr (lrg_sprite_animation_get_event (clip, 0, &frame_pos), ==, "footstep");
		g_assert_cmpuint (frame_pos, ==, 0);
	}

	/* The strip selects positions; Play advances the preview */
	efx_frames (f, 2);
	g_assert_nonnull (efx_find (f, "sprite-strip"));
	ipc_ok (f, "{'id':7,'method':'sprite.play','params':{'clip':'run','time':0.0,'playing':true}}");
	g_assert_true (lrg_editor_sprite_editor_get_playing (ed));
	efx_frames (f, 9);   /* 9 frames at 1/60 s: 0.15 s -> position 1 */
	g_assert_cmpint (lrg_editor_sprite_editor_get_preview_frame (ed, &pos), ==, 0);
	g_assert_cmpuint (pos, ==, 1);
	lrg_editor_sprite_editor_set_playing (ed, FALSE);
	reveal (f, "sprite-scroll", "sprite-play");
	lrg_editor_sprite_editor_set_playing (ed, TRUE);
	efx_click_widget (f, find (f, "sprite-play"));
	g_assert_false (lrg_editor_sprite_editor_get_playing (ed));
	lrg_editor_sprite_editor_seek (ed, 0.3f);
	g_assert_cmpint (lrg_editor_sprite_editor_get_preview_frame (ed, &pos), ==, 3);
	g_assert_cmpuint (pos, ==, 2);

	/* Loop mode and fps; removing a frame used by the clip drops it */
	ipc_ok (f, "{'id':8,'method':'sprite.clip','params':{'op':'set','name':'run','loop':'ping-pong','fps':12}}");
	clip = lrg_sprite_sheet_get_clip (lrg_editor_sprite_editor_get_sheet (ed), "run");
	g_assert_cmpint (lrg_sprite_animation_get_loop_mode (clip), ==, LRG_SPRITE_LOOP_PING_PONG);
	g_assert_true (ipc_fails (f, "{'id':9,'method':'sprite.frame','params':{'op':'remove','index':0}}") == FALSE);
	clip = lrg_sprite_sheet_get_clip (lrg_editor_sprite_editor_get_sheet (ed), "run");
	g_assert_cmpuint (lrg_sprite_animation_get_n_frames (clip), ==, 2);

	/* Save and load with the engine */
	ipc_ok (f, "{'id':10,'method':'sprite.save'}");
	saved = lrg_sprite_sheet_new_from_rsprite_file (path, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_sprite_sheet_get_frame_count (saved), ==, 3);
	g_assert_nonnull (lrg_sprite_sheet_get_clip (saved, "run"));
	g_assert_nonnull (lrg_sprite_sheet_get_frame_by_name (saved, "sword"));
	r = efx_ipc_result (f, "{'id':11,'method':'sprite.get'}");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (json_node_get_object (r), "clips")), ==, 1);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add ("/editor-app-2d/tiles/transform", EditorAppFixture, NULL, efx_setup, test_tiles_transform, efx_teardown);
	g_test_add ("/editor-app-2d/tiles/rules", EditorAppFixture, NULL, efx_setup, test_tiles_rules, efx_teardown);
	g_test_add ("/editor-app-2d/tiles/iso-hex", EditorAppFixture, NULL, efx_setup, test_tiles_iso_hex, efx_teardown);
	g_test_add ("/editor-app-2d/tiles/viewport", EditorAppFixture, NULL, efx_setup, test_tiles_viewport, efx_teardown);
	g_test_add ("/editor-app-2d/tiles/palette", EditorAppFixture, NULL, efx_setup, test_tiles_palette, efx_teardown);
	g_test_add ("/editor-app-2d/tileset/edit", EditorAppFixture, NULL, efx_setup, test_tileset_edit, efx_teardown);
	g_test_add ("/editor-app-2d/sprite/edit", EditorAppFixture, NULL, efx_setup, test_sprite_edit, efx_teardown);
	return g_test_run ();
}
