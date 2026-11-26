/* test-tiled.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the Tiled importer (LrgTiledMap).  tests/fixtures/tiled/
 * holds one office map written twice, as .tmj (with a .tsj external
 * tileset, zlib and gzip layer data) and as .tmx (with a .tsx tileset,
 * CSV, zlib and plain base64 data); every content test runs on both,
 * and one test checks that they agree cell for cell.
 */

#include <glib.h>
#include <libregnum.h>
#include <string.h>

static gchar *
fixture (const gchar *name)
{
	return g_test_build_filename (G_TEST_DIST, "fixtures", "tiled", name, NULL);
}

static LrgTiledMap *
load (const gchar *name)
{
	g_autofree gchar *path = fixture (name);
	g_autoptr(GError) error = NULL;
	LrgTiledMap *map;

	map = lrg_tiled_map_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (map);
	return map;
}

/* ==========================================================================
 * Content (run for both formats)
 * ========================================================================== */

static void
test_map_header (gconstpointer data)
{
	g_autoptr(LrgTiledMap) map = load (data);
	LrgTiledProperties *props;
	const gchar *music;
	guint tw;
	guint th;

	g_assert_cmpuint (lrg_tiled_map_get_width (map), ==, 10);
	g_assert_cmpuint (lrg_tiled_map_get_height (map), ==, 8);
	lrg_tiled_map_get_tile_size (map, &tw, &th);
	g_assert_cmpuint (tw, ==, 16);
	g_assert_cmpuint (th, ==, 16);
	g_assert_cmpint (lrg_tiled_map_get_orientation (map), ==, LRG_TILEMAP_ORIENTATION_ORTHOGONAL);
	g_assert_cmpstr (lrg_tiled_map_get_background_color (map), ==, "#202830");

	props = lrg_tiled_map_get_properties (map);
	g_assert_cmpstr (lrg_tiled_properties_get_string (props, "title"), ==, "Office");
	g_assert_cmpint (lrg_tiled_properties_get_int (props, "floor", -1), ==, 1);
	g_assert_cmpint (lrg_tiled_properties_get_value_type (props, "floor"), ==, LRG_TILED_PROPERTY_INT);

	/* file properties are resolved against the map's directory */
	music = lrg_tiled_properties_get_string (props, "music");
	g_assert_true (g_path_is_absolute (music));
	{
		/* either separator: Windows paths use '\\' */
		g_autofree gchar *norm = g_strdelimit (g_strdup (music), "\\", '/');

		g_assert_true (g_str_has_suffix (norm, "/tiled/sounds/hum.ogg"));
	}
	g_assert_cmpint (lrg_tiled_properties_get_value_type (props, "music"), ==, LRG_TILED_PROPERTY_FILE);

	/* class properties are flattened */
	g_assert_cmpint (lrg_tiled_properties_get_int (props, "spawn.x", 0), ==, 3);
	g_assert_cmpint (lrg_tiled_properties_get_int (props, "spawn.y", 0), ==, 4);
	g_assert_cmpstr (lrg_tiled_properties_get_string (props, "spawn.facing"), ==, "north");
	g_assert_false (lrg_tiled_properties_has (props, "spawn"));
	g_assert_null (lrg_tiled_properties_get_string (props, "nope"));
	g_assert_cmpint (lrg_tiled_properties_get_int (props, "nope", 77), ==, 77);
	g_assert_cmpint (lrg_tiled_properties_get_int (props, "title", 5), ==, 5);
}

static void
test_property_keys_sorted (gconstpointer data)
{
	g_autoptr(LrgTiledMap) map = load (data);
	g_auto(GStrv) keys = NULL;

	keys = lrg_tiled_properties_get_keys (lrg_tiled_map_get_properties (map));
	g_assert_cmpuint (g_strv_length (keys), ==, 6);
	g_assert_cmpstr (keys[0], ==, "floor");
	g_assert_cmpstr (keys[1], ==, "music");
	g_assert_cmpstr (keys[2], ==, "spawn.facing");
	g_assert_cmpstr (keys[3], ==, "spawn.x");
	g_assert_cmpstr (keys[4], ==, "spawn.y");
	g_assert_cmpstr (keys[5], ==, "title");
}

static void
test_tilesets (gconstpointer data)
{
	g_autoptr(LrgTiledMap) map = load (data);
	LrgTiledTileset *ts;
	LrgTiledTileset *found;
	LrgTiledProperties *tp;
	guint local = 99;
	guint w;
	guint h;

	g_assert_cmpuint (lrg_tiled_map_get_n_tilesets (map), ==, 2);

	ts = lrg_tiled_map_get_tileset (map, 0);
	g_assert_cmpstr (lrg_tiled_tileset_get_name (ts), ==, "office");
	g_assert_cmpuint (lrg_tiled_tileset_get_first_gid (ts), ==, 1);
	g_assert_cmpuint (lrg_tiled_tileset_get_tile_count (ts), ==, 8);
	g_assert_cmpuint (lrg_tiled_tileset_get_columns (ts), ==, 4);
	lrg_tiled_tileset_get_tile_size (ts, &w, &h);
	g_assert_cmpuint (w, ==, 16);
	g_assert_cmpuint (h, ==, 16);
	g_assert_true (g_path_is_absolute (lrg_tiled_tileset_get_image_path (ts)));
	g_assert_true (g_str_has_suffix (lrg_tiled_tileset_get_image_path (ts), "office-tiles.png"));
	g_assert_cmpstr (lrg_tiled_properties_get_string (lrg_tiled_tileset_get_properties (ts), "theme"),
	                 ==, "day");
	g_assert_cmpstr (lrg_tiled_tileset_get_tile_type (ts, 1), ==, "wall");
	tp = lrg_tiled_tileset_get_tile_properties (ts, 1);
	g_assert_nonnull (tp);
	g_assert_true (lrg_tiled_properties_get_boolean (tp, "solid", FALSE));
	g_assert_null (lrg_tiled_tileset_get_tile_properties (ts, 5));

	/* the external tileset (.tsj / .tsx) */
	ts = lrg_tiled_map_get_tileset (map, 1);
	g_assert_cmpstr (lrg_tiled_tileset_get_name (ts), ==, "office-props");
	g_assert_cmpuint (lrg_tiled_tileset_get_first_gid (ts), ==, 9);
	g_assert_cmpstr (lrg_tiled_tileset_get_tile_type (ts, 0), ==, "chair");

	found = lrg_tiled_map_get_tileset_for_gid (map, 10 | LRG_TILE_FLIP_HORIZONTAL, &local);
	g_assert_true (found == ts);
	g_assert_cmpuint (local, ==, 1);
	g_assert_true (lrg_tiled_map_get_tileset_for_gid (map, 8, &local) ==
	               lrg_tiled_map_get_tileset (map, 0));
	g_assert_cmpuint (local, ==, 7);
	g_assert_null (lrg_tiled_map_get_tileset_for_gid (map, 0, NULL));
	g_assert_null (lrg_tiled_map_get_tileset_for_gid (map, 17, NULL));
}

static void
test_tile_layers (gconstpointer data)
{
	g_autoptr(LrgTiledMap) map = load (data);
	LrgTiledLayer *layer;
	gdouble a;
	gdouble b;
	guint n = 0;

	g_assert_cmpuint (lrg_tiled_map_get_n_layers (map), ==, 5);

	layer = lrg_tiled_map_get_layer (map, 0);
	g_assert_cmpint (lrg_tiled_layer_get_kind (layer), ==, LRG_TILED_LAYER_TILES);
	g_assert_cmpstr (lrg_tiled_layer_get_name (layer), ==, "floor");
	g_assert_cmpuint (lrg_tiled_layer_get_width (layer), ==, 10);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 0, 0), ==, 1u | LRG_TILE_FLIP_HORIZONTAL);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 1, 0), ==,
	                  1u | LRG_TILE_FLIP_VERTICAL | LRG_TILE_FLIP_DIAGONAL);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 9, 7), ==, 1);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 10, 0), ==, 0);
	g_assert_nonnull (lrg_tiled_layer_get_gids (layer, &n));
	g_assert_cmpuint (n, ==, 80);

	/* group: the name path, opacity, offset are folded into the child */
	layer = lrg_tiled_map_get_layer_by_name (map, "furniture/walls");
	g_assert_nonnull (layer);
	g_assert_cmpfloat_with_epsilon (lrg_tiled_layer_get_opacity (layer), 0.5, 1e-9);
	lrg_tiled_layer_get_offset (layer, &a, &b);
	g_assert_cmpfloat_with_epsilon (a, 2.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (b, 4.0, 1e-9);
	g_assert_true (lrg_tiled_properties_get_boolean (lrg_tiled_layer_get_properties (layer),
	                                                 "collision", FALSE));
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 0, 0), ==, 2);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 4, 7), ==, 0);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 5, 7), ==, 2);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 5, 5), ==, 0);

	layer = lrg_tiled_map_get_layer_by_name (map, "furniture/props");
	g_assert_false (lrg_tiled_layer_get_visible (layer));
	lrg_tiled_layer_get_parallax (layer, &a, &b);
	g_assert_cmpfloat_with_epsilon (a, 0.5, 1e-9);
	g_assert_cmpfloat_with_epsilon (b, 1.0, 1e-9);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 3, 3), ==, 9);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 4, 3), ==, 10u | LRG_TILE_FLIP_HORIZONTAL);
	g_assert_null (lrg_tiled_map_get_layer_by_name (map, "walls"));

	layer = lrg_tiled_map_get_layer_by_name (map, "backdrop");
	g_assert_cmpint (lrg_tiled_layer_get_kind (layer), ==, LRG_TILED_LAYER_IMAGE);
	{
		/* either separator: Windows paths use '\\' */
		g_autofree gchar *image = g_strdelimit (g_strdup (lrg_tiled_layer_get_image_path (layer)), "\\", '/');

		g_assert_true (g_str_has_suffix (image, "/office-tiles.png"));
	}
	g_assert_null (lrg_tiled_layer_get_gids (layer, &n));
	g_assert_cmpuint (n, ==, 0);
}

static void
test_objects (gconstpointer data)
{
	g_autoptr(LrgTiledMap) map = load (data);
	g_autoptr(GPtrArray) zones = NULL;
	LrgTiledObject *o;
	LrgTiledProperties *p;
	const gfloat *pts;
	guint n = 0;
	gdouble x;
	gdouble y;
	gdouble w;
	gdouble h;

	o = lrg_tiled_map_find_object (map, "meeting");
	g_assert_nonnull (o);
	g_assert_cmpuint (lrg_tiled_object_get_id (o), ==, 1);
	g_assert_cmpstr (lrg_tiled_object_get_object_type (o), ==, "zone");
	g_assert_cmpint (lrg_tiled_object_get_shape (o), ==, LRG_TILED_OBJECT_RECTANGLE);
	lrg_tiled_object_get_bounds (o, &x, &y, &w, &h);
	g_assert_cmpfloat (x, ==, 32.0);
	g_assert_cmpfloat (y, ==, 32.0);
	g_assert_cmpfloat (w, ==, 64.0);
	g_assert_cmpfloat (h, ==, 48.0);
	lrg_tiled_object_get_center (o, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 64.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (y, 56.0, 1e-9);
	p = lrg_tiled_object_get_properties (o);
	g_assert_cmpint (lrg_tiled_properties_get_int (p, "seats", 0), ==, 4);
	g_assert_cmpstr (lrg_tiled_properties_get_string (p, "label"), ==, "Big room");
	g_assert_cmpstr (lrg_tiled_properties_get_string (p, "tint"), ==, "#ff336699");
	g_assert_cmpint (lrg_tiled_properties_get_value_type (p, "tint"), ==, LRG_TILED_PROPERTY_COLOR);
	g_assert_true (lrg_tiled_object_contains_point (o, 32.0, 32.0));
	g_assert_true (lrg_tiled_object_contains_point (o, 96.0, 80.0));
	g_assert_false (lrg_tiled_object_contains_point (o, 97.0, 50.0));

	o = lrg_tiled_map_find_object (map, "desk-1");
	g_assert_cmpint (lrg_tiled_object_get_shape (o), ==, LRG_TILED_OBJECT_POINT);
	lrg_tiled_object_get_center (o, &x, &y);
	g_assert_cmpfloat (x, ==, 120.0);
	g_assert_cmpfloat (y, ==, 40.0);
	g_assert_false (lrg_tiled_object_contains_point (o, 120.0, 40.0));
	p = lrg_tiled_object_get_properties (o);
	g_assert_cmpstr (lrg_tiled_properties_get_string (p, "owner"), ==, "kudu");
	g_assert_true (lrg_tiled_properties_get_boolean (p, "standing", FALSE));
	g_assert_true (lrg_tiled_map_find_object_by_id (map, 2) == o);
	g_assert_null (lrg_tiled_map_find_object_by_id (map, 99));

	o = lrg_tiled_map_find_object (map, "lounge");
	g_assert_cmpint (lrg_tiled_object_get_shape (o), ==, LRG_TILED_OBJECT_ELLIPSE);
	g_assert_true (lrg_tiled_object_contains_point (o, 32.0, 104.0));
	g_assert_false (lrg_tiled_object_contains_point (o, 16.5, 96.5));

	o = lrg_tiled_map_find_object (map, "kitchen");
	g_assert_cmpint (lrg_tiled_object_get_shape (o), ==, LRG_TILED_OBJECT_POLYGON);
	pts = lrg_tiled_object_get_points (o, &n);
	g_assert_cmpuint (n, ==, 6);
	g_assert_cmpfloat (pts[2], ==, 32.0f);
	g_assert_true (lrg_tiled_object_contains_point (o, 142.0, 85.0));
	g_assert_false (lrg_tiled_object_contains_point (o, 114.0, 110.0));
	g_assert_true (lrg_tiled_object_contains_point (o, 144.0, 96.0));  /* on an edge */

	o = lrg_tiled_map_find_object (map, "path");
	g_assert_cmpint (lrg_tiled_object_get_shape (o), ==, LRG_TILED_OBJECT_POLYLINE);
	g_assert_false (lrg_tiled_object_contains_point (o, 0.0, 0.0));

	/* a tile object's position is its bottom-left corner */
	o = lrg_tiled_map_find_object (map, "plant");
	g_assert_cmpint (lrg_tiled_object_get_shape (o), ==, LRG_TILED_OBJECT_TILE);
	g_assert_cmpuint (lrg_tiled_object_get_gid (o), ==, 3);
	lrg_tiled_object_get_center (o, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 152.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (y, 104.0, 1e-9);
	g_assert_true (lrg_tiled_object_contains_point (o, 150.0, 100.0));
	g_assert_false (lrg_tiled_object_contains_point (o, 150.0, 114.0));

	/* rotated 90 degrees clockwise around its own corner */
	o = lrg_tiled_map_find_object (map, "tilted");
	g_assert_cmpfloat (lrg_tiled_object_get_rotation (o), ==, 90.0);
	lrg_tiled_object_get_center (o, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 95.0, 1e-9);
	g_assert_cmpfloat_with_epsilon (y, 20.0, 1e-9);
	g_assert_true (lrg_tiled_object_contains_point (o, 95.0, 25.0));
	g_assert_false (lrg_tiled_object_contains_point (o, 110.0, 12.0));
	g_assert_cmpfloat (lrg_tiled_properties_get_double (lrg_tiled_object_get_properties (o),
	                                                    "weight", 0.0), ==, 2.5);

	zones = lrg_tiled_map_get_objects (map, "zone");
	g_assert_cmpuint (zones->len, ==, 4);
	g_assert_cmpstr (lrg_tiled_object_get_name (g_ptr_array_index (zones, 0)), ==, "meeting");
	g_ptr_array_unref (g_steal_pointer (&zones));
	zones = lrg_tiled_map_get_objects (map, NULL);
	g_assert_cmpuint (zones->len, ==, 7);

	o = lrg_tiled_map_find_zone_at (map, 40.0, 40.0, "zone");
	g_assert_cmpstr (lrg_tiled_object_get_name (o), ==, "meeting");
	g_assert_null (lrg_tiled_map_find_zone_at (map, 1.0, 1.0, NULL));
	g_assert_null (lrg_tiled_map_find_zone_at (map, 150.0, 100.0, "zone"));
	g_assert_cmpstr (lrg_tiled_object_get_name (lrg_tiled_map_find_zone_at (map, 150.0, 100.0, NULL)),
	                 ==, "plant");
}

static void
test_build_tilemap (gconstpointer data)
{
	g_autoptr(LrgTiledMap) map = load (data);
	g_autoptr(LrgTilemap) tm0 = NULL;
	g_autoptr(LrgTilemap) tm1 = NULL;
	g_autoptr(GError) error = NULL;
	LrgTilemapLayer *layer;
	LrgTileset *tileset;
	const gfloat *poly;
	guint n = 0;
	gint ox;
	gint oy;

	tm0 = lrg_tiled_map_build_tilemap (map, 0, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_tilemap_get_layer_count (tm0), ==, 3);
	g_assert_cmpuint (lrg_tilemap_get_width (tm0), ==, 10);

	layer = lrg_tilemap_get_layer_by_name (tm0, "floor");
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 0, 0), ==, 1u | LRG_TILE_FLIP_HORIZONTAL);
	g_assert_cmpuint (lrg_tile_get_id (lrg_tilemap_layer_get_tile (layer, 1, 0)), ==, 1);

	layer = lrg_tilemap_get_layer_by_name (tm0, "furniture/walls");
	g_assert_true (lrg_tilemap_layer_get_collision_enabled (layer));
	g_assert_cmpfloat (lrg_tilemap_layer_get_opacity (layer), ==, 0.5f);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 0, 0), ==, 2);

	/* tiles of the other tileset are empty in this tilemap */
	layer = lrg_tilemap_get_layer_by_name (tm0, "furniture/props");
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 3, 3), ==, 0);
	g_assert_false (lrg_tilemap_layer_get_visible (layer));
	g_assert_false (lrg_tilemap_layer_get_collision_enabled (layer));

	/* ... and renumbered in its own */
	tm1 = lrg_tiled_map_build_tilemap (map, 1, &error);
	g_assert_no_error (error);
	layer = lrg_tilemap_get_layer_by_name (tm1, "furniture/props");
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 3, 3), ==, 1);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 4, 3), ==, 2u | LRG_TILE_FLIP_HORIZONTAL);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (lrg_tilemap_get_layer_by_name (tm1, "floor"), 0, 0),
	                  ==, 0);
	lrg_tileset_get_draw_offset (lrg_tilemap_get_tileset (tm1), &ox, &oy);
	g_assert_cmpint (ox, ==, 0);
	g_assert_cmpint (oy, ==, -4);

	/* the LrgTileset carries tile data, animations and collision shapes */
	tileset = lrg_tilemap_get_tileset (tm0);
	g_assert_true (tileset == lrg_tiled_tileset_get_tileset (lrg_tiled_map_get_tileset (map, 0)));
	g_assert_cmpuint (lrg_tileset_get_tile_count (tileset), ==, 8);
	g_assert_cmpstr (lrg_tileset_get_tile_data (tileset, 1, "type"), ==, "wall");
	g_assert_cmpstr (lrg_tileset_get_tile_data (tileset, 1, "solid"), ==, "true");
	g_assert_cmpstr (lrg_tileset_get_tile_data (tileset, 0, "walk"), ==, "true");
	g_assert_cmpuint (lrg_tileset_get_tile_animation_length (tileset, 2), ==, 2);
	g_assert_cmpuint (lrg_tileset_get_tile_collision_polygon_count (tileset, 1), ==, 1);
	poly = lrg_tileset_get_tile_collision_polygon (tileset, 1, 0, &n);
	g_assert_cmpuint (n, ==, 8);
	g_assert_cmpfloat (poly[0], ==, 0.0f);
	g_assert_cmpfloat (poly[1], ==, 8.0f);
	g_assert_cmpfloat (poly[4], ==, 16.0f);
	g_assert_cmpfloat (poly[5], ==, 16.0f);
	g_assert_true (g_str_has_suffix (lrg_tileset_get_image_path (tileset), "office-tiles.png"));

	g_assert_null (lrg_tiled_map_build_tilemap (map, 5, &error));
	g_assert_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_INVALID);
}

/* ==========================================================================
 * The two formats agree
 * ========================================================================== */

static void
test_formats_agree (void)
{
	g_autoptr(LrgTiledMap) tmj = load ("office.tmj");
	g_autoptr(LrgTiledMap) tmx = load ("office.tmx");
	guint i;

	g_assert_cmpuint (lrg_tiled_map_get_n_layers (tmj), ==, lrg_tiled_map_get_n_layers (tmx));
	for (i = 0; i < lrg_tiled_map_get_n_layers (tmj); i++)
	{
		LrgTiledLayer *a = lrg_tiled_map_get_layer (tmj, i);
		LrgTiledLayer *b = lrg_tiled_map_get_layer (tmx, i);
		const guint *ga;
		const guint *gb;
		guint na = 0;
		guint nb = 0;
		guint k;

		g_assert_cmpstr (lrg_tiled_layer_get_name (a), ==, lrg_tiled_layer_get_name (b));
		g_assert_cmpint (lrg_tiled_layer_get_kind (a), ==, lrg_tiled_layer_get_kind (b));
		ga = lrg_tiled_layer_get_gids (a, &na);
		gb = lrg_tiled_layer_get_gids (b, &nb);
		g_assert_cmpuint (na, ==, nb);
		if (na > 0)
			g_assert_cmpmem (ga, na * sizeof (guint), gb, nb * sizeof (guint));

		g_assert_cmpuint (lrg_tiled_layer_get_n_objects (a), ==, lrg_tiled_layer_get_n_objects (b));
		for (k = 0; k < lrg_tiled_layer_get_n_objects (a); k++)
		{
			LrgTiledObject *oa = lrg_tiled_layer_get_object (a, k);
			LrgTiledObject *ob = lrg_tiled_layer_get_object (b, k);
			gdouble ax;
			gdouble ay;
			gdouble bx;
			gdouble by;

			g_assert_cmpstr (lrg_tiled_object_get_name (oa), ==, lrg_tiled_object_get_name (ob));
			g_assert_cmpint (lrg_tiled_object_get_shape (oa), ==, lrg_tiled_object_get_shape (ob));
			lrg_tiled_object_get_center (oa, &ax, &ay);
			lrg_tiled_object_get_center (ob, &bx, &by);
			g_assert_cmpfloat_with_epsilon (ax, bx, 1e-6);
			g_assert_cmpfloat_with_epsilon (ay, by, 1e-6);
			g_assert_cmpuint (lrg_tiled_properties_get_n_properties (lrg_tiled_object_get_properties (oa)),
			                  ==,
			                  lrg_tiled_properties_get_n_properties (lrg_tiled_object_get_properties (ob)));
		}
	}
}

/* ==========================================================================
 * Errors and less common encodings
 * ========================================================================== */

static void
expect_error (const gchar *doc,
              gint         code)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgTiledMap) map = NULL;

	map = lrg_tiled_map_new_from_data (doc, -1, NULL, &error);
	g_assert_null (map);
	g_assert_error (error, LRG_TILED_ERROR, code);
}

static void
test_errors (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgTiledMap) map = NULL;

	expect_error ("not a map", LRG_TILED_ERROR_PARSE);
	expect_error ("{ \"width\": ", LRG_TILED_ERROR_PARSE);
	expect_error ("<map><oops></map>", LRG_TILED_ERROR_PARSE);
	expect_error ("<tileset/>", LRG_TILED_ERROR_INVALID);
	expect_error ("{\"type\":\"map\",\"infinite\":true,\"width\":2,\"height\":2,"
	              "\"tilewidth\":8,\"tileheight\":8,\"layers\":[]}", LRG_TILED_ERROR_UNSUPPORTED);
	expect_error ("<map width=\"2\" height=\"2\" tilewidth=\"8\" tileheight=\"8\" infinite=\"1\"/>",
	              LRG_TILED_ERROR_UNSUPPORTED);
	expect_error ("{\"type\":\"map\",\"width\":0,\"height\":2,\"tilewidth\":8,\"tileheight\":8,"
	              "\"layers\":[]}", LRG_TILED_ERROR_INVALID);
	/* wrong cell count */
	expect_error ("{\"type\":\"map\",\"width\":2,\"height\":2,\"tilewidth\":8,\"tileheight\":8,"
	              "\"layers\":[{\"type\":\"tilelayer\",\"name\":\"a\",\"width\":2,\"height\":2,"
	              "\"data\":[1,2,3]}]}", LRG_TILED_ERROR_INVALID);
	expect_error ("<map width=\"2\" height=\"1\" tilewidth=\"8\" tileheight=\"8\"><layer name=\"a\" "
	              "width=\"2\" height=\"1\"><data encoding=\"csv\">1,2,3</data></layer></map>",
	              LRG_TILED_ERROR_INVALID);
	/* zstd is valid Tiled, but not read here */
	expect_error ("{\"type\":\"map\",\"width\":1,\"height\":1,\"tilewidth\":8,\"tileheight\":8,"
	              "\"layers\":[{\"type\":\"tilelayer\",\"name\":\"a\",\"width\":1,\"height\":1,"
	              "\"encoding\":\"base64\",\"compression\":\"zstd\",\"data\":\"AAAA\"}]}",
	              LRG_TILED_ERROR_UNSUPPORTED);
	/* templates */
	expect_error ("<map width=\"1\" height=\"1\" tilewidth=\"8\" tileheight=\"8\"><objectgroup "
	              "name=\"o\"><object id=\"1\" template=\"chair.tx\" x=\"0\" y=\"0\"/></objectgroup></map>",
	              LRG_TILED_ERROR_UNSUPPORTED);
	/* a missing external tileset */
	expect_error ("<map width=\"1\" height=\"1\" tilewidth=\"8\" tileheight=\"8\"><tileset "
	              "firstgid=\"1\" source=\"/nonexistent/x.tsx\"/></map>", LRG_TILED_ERROR_IO);

	map = lrg_tiled_map_new_from_file ("/nonexistent/map.tmj", &error);
	g_assert_null (map);
	g_assert_error (error, LRG_TILED_ERROR, LRG_TILED_ERROR_IO);
}

static void
test_tile_elements_and_staggered (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgTiledMap) map = NULL;
	LrgTiledLayer *layer;
	const gchar *doc =
		"<?xml version=\"1.0\"?>\n"
		"<map orientation=\"hexagonal\" staggeraxis=\"x\" staggerindex=\"even\" hexsidelength=\"6\" "
		"width=\"2\" height=\"1\" tilewidth=\"8\" tileheight=\"8\">\n"
		" <layer name=\"old\" width=\"2\" height=\"1\"><data><tile gid=\"5\"/><tile/></data></layer>\n"
		" <layer name=\"txt\" width=\"2\" height=\"1\"><properties>"
		"<property name=\"note\">line one\nline two</property></properties>"
		"<data encoding=\"csv\">0,268435457</data></layer>\n"
		"</map>\n";

	map = lrg_tiled_map_new_from_data (doc, -1, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (lrg_tiled_map_get_orientation (map), ==, LRG_TILEMAP_ORIENTATION_HEXAGONAL);
	layer = lrg_tiled_map_get_layer (map, 0);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 0, 0), ==, 5);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 1, 0), ==, 0);
	layer = lrg_tiled_map_get_layer (map, 1);
	/* the hexagonal 120-degree bit is kept in the raw gid */
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 1, 0), ==, 1u | LRG_TILED_GID_FLIP_HEXAGONAL);
	g_assert_cmpuint (lrg_tiled_layer_get_gid (layer, 1, 0) & LRG_TILED_GID_MASK, ==, 1);
	g_assert_cmpstr (lrg_tiled_properties_get_string (lrg_tiled_layer_get_properties (layer), "note"),
	                 ==, "line one\nline two");
}

int
main (int   argc,
      char *argv[])
{
	static const gchar *formats[] = { "office.tmj", "office.tmx" };
	guint i;

	g_test_init (&argc, &argv, NULL);

	for (i = 0; i < G_N_ELEMENTS (formats); i++)
	{
		const gchar *ext = strrchr (formats[i], '.') + 1;
		g_autofree gchar *p1 = g_strdup_printf ("/tiled/%s/header", ext);
		g_autofree gchar *p2 = g_strdup_printf ("/tiled/%s/property-keys", ext);
		g_autofree gchar *p3 = g_strdup_printf ("/tiled/%s/tilesets", ext);
		g_autofree gchar *p4 = g_strdup_printf ("/tiled/%s/tile-layers", ext);
		g_autofree gchar *p5 = g_strdup_printf ("/tiled/%s/objects", ext);
		g_autofree gchar *p6 = g_strdup_printf ("/tiled/%s/build-tilemap", ext);

		g_test_add_data_func (p1, formats[i], test_map_header);
		g_test_add_data_func (p2, formats[i], test_property_keys_sorted);
		g_test_add_data_func (p3, formats[i], test_tilesets);
		g_test_add_data_func (p4, formats[i], test_tile_layers);
		g_test_add_data_func (p5, formats[i], test_objects);
		g_test_add_data_func (p6, formats[i], test_build_tilemap);
	}
	g_test_add_func ("/tiled/formats-agree", test_formats_agree);
	g_test_add_func ("/tiled/errors", test_errors);
	g_test_add_func ("/tiled/tile-elements-and-staggered", test_tile_elements_and_staggered);

	return g_test_run ();
}
