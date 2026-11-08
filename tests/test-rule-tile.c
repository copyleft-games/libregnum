/* test-rule-tile.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for the 0.9.0 2D tile work:
 *
 * - per-tile transform bits (lrg_tile_*);
 * - rule tiles: 16/47-tile presets, weighted variants, rotated and
 *   mirrored rules, painting and erasing, hexagonal/staggered neighbours;
 * - the .rtileset format: byte-stable round trip, unknown keys kept,
 *   errors with key paths, per-tile collision/data/animation;
 * - isometric, staggered and hexagonal tilemaps: cell placement, exact
 *   picking, outlines, draw order; rule painting through LrgTilemap;
 * - shadow occluders built from tilemap collision.
 */

#include <glib.h>
#include <libregnum.h>
#include <string.h>
#include <glib/gstdio.h>
#include <raylib.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* a w x h layer of zeros, row-major */
static guint *
new_tiles (guint w,
           guint h)
{
	return g_new0 (guint, (gsize) w * h);
}

#define AT(tiles, w, x, y) ((tiles)[(gsize) (y) * (w) + (x)])

/* ==========================================================================
 * Transform bits
 * ========================================================================== */

static void
test_transform_bits (void)
{
	guint m;
	guint r;

	/* every (mirror, rotation) pair round-trips and is distinct */
	for (m = 0; m < 2; m++)
	{
		for (r = 0; r < 4; r++)
		{
			guint t = lrg_tile_make (5, m != 0, r);

			g_assert_cmpuint (lrg_tile_get_id (t), ==, 5);
			g_assert_cmpuint (lrg_tile_get_rotation (t), ==, r);
			g_assert_cmpint (lrg_tile_get_mirrored (t), ==, m != 0);
		}
	}

	/* Tiled's encoding: 90 degrees clockwise is diagonal + horizontal */
	g_assert_cmpuint (lrg_tile_make (5, FALSE, 1), ==, 5u | LRG_TILE_FLIP_DIAGONAL | LRG_TILE_FLIP_HORIZONTAL);
	g_assert_cmpuint (lrg_tile_make (5, FALSE, 2), ==, 5u | LRG_TILE_FLIP_HORIZONTAL | LRG_TILE_FLIP_VERTICAL);
	g_assert_cmpuint (lrg_tile_make (5, TRUE, 0), ==, 5u | LRG_TILE_FLIP_HORIZONTAL);
	g_assert_cmpuint (lrg_tile_make (5, TRUE, 2), ==, 5u | LRG_TILE_FLIP_VERTICAL);
	g_assert_cmpuint (lrg_tile_make (0, TRUE, 1), ==, 0);

	/* composition */
	g_assert_cmpuint (lrg_tile_rotate (lrg_tile_rotate (7, 3), 1), ==, 7);
	g_assert_cmpuint (lrg_tile_rotate (7, -1), ==, lrg_tile_make (7, FALSE, 3));
	g_assert_cmpuint (lrg_tile_flip_horizontally (lrg_tile_flip_horizontally (9)), ==, 9);
	g_assert_cmpuint (lrg_tile_flip_vertically (9), ==, lrg_tile_rotate (lrg_tile_flip_horizontally (9), 2));
	g_assert_cmpuint (lrg_tile_flip_vertically (9), ==, 9u | LRG_TILE_FLIP_VERTICAL);
	g_assert_cmpuint (lrg_tile_flip_horizontally (lrg_tile_rotate (3, 1)), ==, lrg_tile_make (3, TRUE, 3));
}

static void
test_transform_point (void)
{
	gfloat x;
	gfloat y;

	/* a quarter turn clockwise takes the top-left corner to the top-right */
	x = 0.0f;
	y = 0.0f;
	lrg_tile_transform_point (lrg_tile_make (1, FALSE, 1), 16.0f, 16.0f, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 16.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (y, 0.0f, 1e-4);

	/* mirrored: left edge to right edge */
	x = 2.0f;
	y = 5.0f;
	lrg_tile_transform_point (lrg_tile_make (1, TRUE, 0), 16.0f, 16.0f, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 14.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (y, 5.0f, 1e-4);

	/* vertical flip */
	x = 2.0f;
	y = 5.0f;
	lrg_tile_transform_point (lrg_tile_flip_vertically (1), 16.0f, 16.0f, &x, &y);
	g_assert_cmpfloat_with_epsilon (x, 2.0f, 1e-4);
	g_assert_cmpfloat_with_epsilon (y, 11.0f, 1e-4);
}

static void
test_layer_transform (void)
{
	g_autoptr(LrgTilemapLayer) layer = lrg_tilemap_layer_new (4, 4);

	lrg_tilemap_layer_set_tile (layer, 1, 1, 3);
	lrg_tilemap_layer_set_tile_transform (layer, 1, 1, TRUE, 1);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile_id (layer, 1, 1), ==, 3);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 1, 1), ==, lrg_tile_make (3, TRUE, 1));

	/* empty cells stay empty */
	lrg_tilemap_layer_set_tile_transform (layer, 0, 0, TRUE, 1);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 0, 0), ==, 0);
}

/* ==========================================================================
 * Rule tiles
 * ========================================================================== */

static void
test_edges16 (void)
{
	g_autoptr(LrgRuleTile) rt = lrg_rule_tile_new_edges16 ("wall", 1);
	g_autofree guint *tiles = new_tiles (5, 5);
	guint i;

	g_assert_cmpuint (lrg_rule_tile_get_n_rules (rt), ==, 16);

	/* a plus sign */
	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 2, 2);
	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 2, 1);
	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 2, 3);
	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 1, 2);
	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 3, 2);

	/* edge mask N=1 E=2 S=4 W=8, tile = 1 + mask */
	g_assert_cmpuint (AT (tiles, 5, 2, 2), ==, 1 + 15);
	g_assert_cmpuint (AT (tiles, 5, 2, 1), ==, 1 + 4);   /* only S */
	g_assert_cmpuint (AT (tiles, 5, 2, 3), ==, 1 + 1);   /* only N */
	g_assert_cmpuint (AT (tiles, 5, 1, 2), ==, 1 + 2);   /* only E */
	g_assert_cmpuint (AT (tiles, 5, 3, 2), ==, 1 + 8);   /* only W */
	g_assert_cmpuint (AT (tiles, 5, 0, 0), ==, 0);

	/* every cell is owned; untouched cells stay empty */
	for (i = 0; i < 25; i++)
		if (tiles[i] != 0)
			g_assert_true (lrg_rule_tile_owns_tile (rt, tiles[i]));
}

static void
test_blob47 (void)
{
	g_autoptr(LrgRuleTile) rt = lrg_rule_tile_new_blob47 ("ground", 10);
	g_autofree guint *tiles = new_tiles (5, 5);
	guint rank;
	guint x;
	guint y;

	/* the 47 masks are distinct, ascending and self-reducing */
	for (rank = 0; rank < LRG_RULE_TILE_BLOB47_COUNT; rank++)
	{
		LrgTileNeighbor mask = lrg_rule_tile_blob47_mask (rank);

		g_assert_cmpuint (lrg_rule_tile_blob47_rank (mask), ==, rank);
		if (rank > 0)
			g_assert_cmpuint (mask, >, lrg_rule_tile_blob47_mask (rank - 1));
	}
	g_assert_cmpuint (lrg_rule_tile_blob47_mask (0), ==, 0);
	g_assert_cmpuint (lrg_rule_tile_blob47_mask (46), ==, LRG_TILE_NEIGHBOR_ALL);
	/* a lone corner does not count */
	g_assert_cmpuint (lrg_rule_tile_blob47_rank (LRG_TILE_NEIGHBOR_NE), ==, 0);

	/* a 3x3 block */
	for (y = 1; y <= 3; y++)
		for (x = 1; x <= 3; x++)
			lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, (gint) x, (gint) y);

	g_assert_cmpuint (AT (tiles, 5, 2, 2), ==, 10 + 46);
	g_assert_cmpuint (AT (tiles, 5, 1, 1), ==,
	                  10 + lrg_rule_tile_blob47_rank (LRG_TILE_NEIGHBOR_E | LRG_TILE_NEIGHBOR_SE | LRG_TILE_NEIGHBOR_S));
	g_assert_cmpuint (AT (tiles, 5, 2, 1), ==,
	                  10 + lrg_rule_tile_blob47_rank (LRG_TILE_NEIGHBOR_E | LRG_TILE_NEIGHBOR_SE | LRG_TILE_NEIGHBOR_S |
	                                                  LRG_TILE_NEIGHBOR_SW | LRG_TILE_NEIGHBOR_W));

	/* erasing the centre opens the ring: the top middle loses S */
	lrg_rule_tile_erase (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 2, 2);
	g_assert_cmpuint (AT (tiles, 5, 2, 2), ==, 0);
	g_assert_cmpuint (AT (tiles, 5, 2, 1), ==,
	                  10 + lrg_rule_tile_blob47_rank (LRG_TILE_NEIGHBOR_E | LRG_TILE_NEIGHBOR_W));

	/* border-is-this: the map edge counts as ground */
	lrg_rule_tile_set_border_is_this (rt, TRUE);
	memset (tiles, 0, 25 * sizeof (guint));
	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 0, 0);
	g_assert_cmpuint (AT (tiles, 5, 0, 0), ==,
	                  10 + lrg_rule_tile_blob47_rank (LRG_TILE_NEIGHBOR_N | LRG_TILE_NEIGHBOR_NE | LRG_TILE_NEIGHBOR_W |
	                                                  LRG_TILE_NEIGHBOR_NW | LRG_TILE_NEIGHBOR_SW));
}

static void
test_variants (void)
{
	g_autoptr(LrgRuleTile) rt = lrg_rule_tile_new ("grass");
	g_autoptr(LrgTileRule) rule = lrg_tile_rule_new ();
	g_autofree guint *a = new_tiles (64, 64);
	g_autofree guint *b = NULL;
	guint count_2 = 0;
	guint changed;
	guint i;

	lrg_tile_rule_add_output (rule, 2, 3.0f);
	lrg_tile_rule_add_output (rule, 3, 1.0f);
	lrg_rule_tile_add_rule (rt, rule);

	for (i = 0; i < 64 * 64; i++)
		a[i] = 2;
	lrg_rule_tile_apply (rt, a, 64 * 64, 64, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 0, 0, G_MAXUINT, G_MAXUINT);
	for (i = 0; i < 64 * 64; i++)
	{
		g_assert_true (a[i] == 2 || a[i] == 3);
		if (a[i] == 2)
			count_2++;
	}
	/* 3:1 weights: about 75 % */
	g_assert_cmpuint (count_2, >, 64 * 64 * 70 / 100);
	g_assert_cmpuint (count_2, <, 64 * 64 * 80 / 100);

	/* deterministic: applying again changes nothing */
	b = g_memdup2 (a, 64 * 64 * sizeof (guint));
	changed = lrg_rule_tile_apply (rt, b, 64 * 64, 64, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 0, 0, G_MAXUINT, G_MAXUINT);
	g_assert_cmpuint (changed, ==, 0);
	g_assert_cmpmem (a, 64 * 64 * sizeof (guint), b, 64 * 64 * sizeof (guint));

	/* a new seed reshuffles */
	lrg_rule_tile_set_seed (rt, 1234);
	changed = lrg_rule_tile_apply (rt, b, 64 * 64, 64, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 0, 0, G_MAXUINT, G_MAXUINT);
	g_assert_cmpuint (changed, >, 100);
}

static void
test_rotated_and_mirrored (void)
{
	g_autoptr(LrgRuleTile) rt = lrg_rule_tile_new ("pipe");
	g_autoptr(LrgTileRule) cap = lrg_tile_rule_new ();
	g_autoptr(LrgTileRule) corner = lrg_tile_rule_new ();
	g_autoptr(LrgTileRule) any = lrg_tile_rule_new ();
	g_autofree guint *tiles = new_tiles (5, 5);
	guint rotation = 99;
	gboolean mirrored = TRUE;

	/* an end cap drawn pointing up (neighbour N only), matched rotated */
	lrg_tile_rule_set_neighbor (cap, LRG_TILE_NEIGHBOR_N, LRG_TILE_RULE_MATCH_THIS);
	lrg_tile_rule_set_neighbor (cap, LRG_TILE_NEIGHBOR_E | LRG_TILE_NEIGHBOR_S | LRG_TILE_NEIGHBOR_W,
	                            LRG_TILE_RULE_MATCH_NOT_THIS);
	lrg_tile_rule_set_transform (cap, LRG_TILE_RULE_TRANSFORM_ROTATED);
	lrg_tile_rule_add_output (cap, 7, 1.0f);
	g_assert_cmpint (lrg_tile_rule_get_neighbor (cap, LRG_TILE_NEIGHBOR_N), ==, LRG_TILE_RULE_MATCH_THIS);
	g_assert_cmpint (lrg_tile_rule_get_neighbor (cap, LRG_TILE_NEIGHBOR_NE), ==, LRG_TILE_RULE_MATCH_ANY);

	g_assert_true (lrg_tile_rule_match (cap, LRG_TILE_NEIGHBOR_E, LRG_TILE_NEIGHBOR_ALL, &rotation, &mirrored));
	g_assert_cmpuint (rotation, ==, 1);
	g_assert_false (mirrored);
	g_assert_false (lrg_tile_rule_match (cap, LRG_TILE_NEIGHBOR_E | LRG_TILE_NEIGHBOR_W,
	                                     LRG_TILE_NEIGHBOR_ALL, NULL, NULL));

	/* a corner drawn as N + E, matched mirrored left-right (N + W) */
	lrg_tile_rule_set_neighbor (corner, LRG_TILE_NEIGHBOR_N | LRG_TILE_NEIGHBOR_E, LRG_TILE_RULE_MATCH_THIS);
	lrg_tile_rule_set_neighbor (corner, LRG_TILE_NEIGHBOR_S | LRG_TILE_NEIGHBOR_W, LRG_TILE_RULE_MATCH_NOT_THIS);
	lrg_tile_rule_set_transform (corner, LRG_TILE_RULE_TRANSFORM_MIRROR_X);
	lrg_tile_rule_add_output (corner, 8, 1.0f);
	g_assert_true (lrg_tile_rule_match (corner, LRG_TILE_NEIGHBOR_N | LRG_TILE_NEIGHBOR_W,
	                                    LRG_TILE_NEIGHBOR_ALL, &rotation, &mirrored));
	g_assert_true (mirrored);
	g_assert_cmpuint (rotation, ==, 0);
	g_assert_false (lrg_tile_rule_match (corner, LRG_TILE_NEIGHBOR_S | LRG_TILE_NEIGHBOR_W,
	                                     LRG_TILE_NEIGHBOR_ALL, NULL, NULL));

	lrg_tile_rule_add_output (any, 9, 1.0f);
	lrg_rule_tile_add_rule (rt, cap);
	lrg_rule_tile_add_rule (rt, corner);
	lrg_rule_tile_add_rule (rt, any);

	/* a horizontal pair: the left end only has E -> cap turned clockwise */
	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 1, 1);
	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 2, 1);
	g_assert_cmpuint (AT (tiles, 5, 1, 1), ==, lrg_tile_make (7, FALSE, 1));
	g_assert_cmpuint (AT (tiles, 5, 2, 1), ==, lrg_tile_make (7, FALSE, 3));

	/* a corner at (2, 2): N (2, 1) and W (1, 2) -> mirrored corner */
	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 2, 2);
	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 1, 2);
	g_assert_cmpuint (lrg_tile_get_id (AT (tiles, 5, 2, 2)), ==, 8);
	g_assert_true (lrg_tile_get_mirrored (AT (tiles, 5, 2, 2)));

	/* rule list editing */
	g_assert_true (lrg_rule_tile_move_rule (rt, 2, 0));
	g_assert_true (lrg_rule_tile_get_rule (rt, 0) == any);
	g_assert_true (lrg_rule_tile_remove_rule (rt, 0));
	g_assert_cmpuint (lrg_rule_tile_get_n_rules (rt), ==, 2);
}

static void
test_neighbors (void)
{
	gint x;
	gint y;

	/* square */
	g_assert_true (lrg_tile_neighbor_offset (LRG_TILE_NEIGHBOR_LAYOUT_SQUARE, 3, 3, LRG_TILE_NEIGHBOR_NE, &x, &y));
	g_assert_cmpint (x, ==, 4);
	g_assert_cmpint (y, ==, 2);

	/* pointy hexagons, odd rows shifted right */
	g_assert_true (lrg_tile_neighbor_offset (LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD, 2, 1, LRG_TILE_NEIGHBOR_NE, &x, &y));
	g_assert_cmpint (x, ==, 3);
	g_assert_cmpint (y, ==, 0);
	g_assert_true (lrg_tile_neighbor_offset (LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD, 2, 2, LRG_TILE_NEIGHBOR_NE, &x, &y));
	g_assert_cmpint (x, ==, 2);
	g_assert_cmpint (y, ==, 1);
	g_assert_true (lrg_tile_neighbor_offset (LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD, 2, 2, LRG_TILE_NEIGHBOR_SW, &x, &y));
	g_assert_cmpint (x, ==, 1);
	g_assert_cmpint (y, ==, 3);
	g_assert_false (lrg_tile_neighbor_offset (LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD, 2, 2, LRG_TILE_NEIGHBOR_N, &x, &y));
	g_assert_cmpuint (lrg_tile_neighbor_layout_get_mask (LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_EVEN) &
	                  (LRG_TILE_NEIGHBOR_N | LRG_TILE_NEIGHBOR_S), ==, 0);

	/* flat hexagons, even columns shifted down */
	g_assert_true (lrg_tile_neighbor_offset (LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_EVEN, 2, 2, LRG_TILE_NEIGHBOR_SE, &x, &y));
	g_assert_cmpint (x, ==, 3);
	g_assert_cmpint (y, ==, 3);
	g_assert_false (lrg_tile_neighbor_offset (LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_EVEN, 2, 2, LRG_TILE_NEIGHBOR_E, &x, &y));

	/* staggered isometric: diamond north is up-right on screen */
	g_assert_true (lrg_tile_neighbor_offset (LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_ODD, 2, 2, LRG_TILE_NEIGHBOR_N, &x, &y));
	g_assert_cmpint (x, ==, 2);
	g_assert_cmpint (y, ==, 1);
	g_assert_true (lrg_tile_neighbor_offset (LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_ODD, 2, 2, LRG_TILE_NEIGHBOR_NE, &x, &y));
	g_assert_cmpint (x, ==, 3);
	g_assert_cmpint (y, ==, 2);
}

static void
test_hex_rule_tile (void)
{
	g_autoptr(LrgRuleTile) rt = lrg_rule_tile_new ("water");
	g_autoptr(LrgTileRule) inner = lrg_tile_rule_new ();
	g_autoptr(LrgTileRule) shore = lrg_tile_rule_new ();
	g_autofree guint *tiles = new_tiles (5, 5);
	gint x;
	gint y;
	guint i;

	/* "all six neighbours are water" -> 2, else 1.  N/S requirements are
	 * ignored on pointy-top hexagons. */
	lrg_tile_rule_set_neighbor (inner, LRG_TILE_NEIGHBOR_ALL, LRG_TILE_RULE_MATCH_THIS);
	lrg_tile_rule_add_output (inner, 2, 1.0f);
	lrg_tile_rule_add_output (shore, 1, 1.0f);
	lrg_rule_tile_add_rule (rt, inner);
	lrg_rule_tile_add_rule (rt, shore);

	lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD, 2, 2);
	for (i = 0; i < 8; i++)
	{
		if (lrg_tile_neighbor_offset (LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD, 2, 2, (LrgTileNeighbor) (1u << i), &x, &y))
			lrg_rule_tile_paint (rt, tiles, 25, 5, LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD, x, y);
	}
	g_assert_cmpuint (AT (tiles, 5, 2, 2), ==, 2);
	g_assert_cmpuint (AT (tiles, 5, 3, 2), ==, 1);
	g_assert_cmpuint (AT (tiles, 5, 2, 1), ==, 0 + 1);  /* NE of an even row is (2, 1) */
	g_assert_cmpuint (AT (tiles, 5, 2, 0), ==, 0);      /* N does not exist: never painted */
}

/* ==========================================================================
 * .rtileset
 * ========================================================================== */

static const gchar *rtileset_fixture =
	"version: 1\n"
	"name: dungeon\n"
	"image: dungeon.png\n"
	"tile_size:\n"
	"- 16\n"
	"- 16\n"
	"columns: 8\n"
	"tile_count: 64\n"
	"spacing: 1\n"
	"offset:\n"
	"- 0\n"
	"- -8\n"
	"tiles:\n"
	"- id: 3\n"
	"  flags:\n"
	"  - solid\n"
	"- id: 5\n"
	"  flags:\n"
	"  - hazard\n"
	"  collision:\n"
	"  - - 0\n"
	"    - 8\n"
	"    - 16\n"
	"    - 8\n"
	"    - 16\n"
	"    - 16\n"
	"    - 0\n"
	"    - 16\n"
	"  data:\n"
	"    kind: water\n"
	"    damage: 2\n"
	"  animation:\n"
	"  - tile: 5\n"
	"    duration: 200\n"
	"  - tile: 6\n"
	"    duration: 100\n"
	"  note: keep me\n"
	"rule_tiles:\n"
	"- name: grass\n"
	"  default: 20\n"
	"  seed: 7\n"
	"  match_tiles:\n"
	"  - 3\n"
	"  rules:\n"
	"  - pattern:\n"
	"    - \".#.\"\n"
	"    - \"-@-\"\n"
	"    - \"...\"\n"
	"    transform: rotated\n"
	"    tiles:\n"
	"    - 21\n"
	"    - tile: 22\n"
	"      weight: 0.5\n"
	"      rotate: 1\n"
	"      flip: true\n"
	"    comment: cap\n"
	"  - tiles:\n"
	"    - 20\n"
	"editor:\n"
	"  zoom: 2\n";

static void
test_rtileset_round_trip (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgTileset) ts = NULL;
	g_autoptr(LrgTileset) again = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *text2 = NULL;
	g_auto(GStrv) keys = NULL;
	LrgRuleTile *rt;
	LrgTileRule *rule;
	const gfloat *poly;
	guint n_coords = 0;
	guint frame = 0;
	guint duration = 0;
	gint ox;
	gint oy;
	GrlRectangle rect;
	gfloat weight = 0.0f;

	ts = lrg_tileset_new_from_asset_data (rtileset_fixture, -1, NULL, "fixture.rtileset", FALSE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (ts);

	g_assert_cmpstr (lrg_tileset_get_name (ts), ==, "dungeon");
	g_assert_cmpstr (lrg_tileset_get_image_path (ts), ==, "dungeon.png");
	g_assert_cmpuint (lrg_tileset_get_tile_count (ts), ==, 64);
	g_assert_cmpuint (lrg_tileset_get_columns (ts), ==, 8);
	g_assert_cmpuint (lrg_tileset_get_spacing (ts), ==, 1);
	lrg_tileset_get_draw_offset (ts, &ox, &oy);
	g_assert_cmpint (oy, ==, -8);

	/* spacing shifts the tile rectangles */
	g_assert_true (lrg_tileset_get_tile_rect_to (ts, 9, &rect));
	g_assert_cmpfloat (rect.x, ==, 17.0f);
	g_assert_cmpfloat (rect.y, ==, 17.0f);

	/* per-tile data */
	g_assert_true (lrg_tileset_tile_has_property (ts, 3, LRG_TILE_PROPERTY_SOLID));
	g_assert_true (lrg_tileset_tile_has_collision (ts, 3));
	g_assert_true (lrg_tileset_tile_has_property (ts, 5, LRG_TILE_PROPERTY_ANIMATED));
	g_assert_true (lrg_tileset_tile_has_collision (ts, 5));
	g_assert_cmpuint (lrg_tileset_get_tile_collision_polygon_count (ts, 5), ==, 1);
	poly = lrg_tileset_get_tile_collision_polygon (ts, 5, 0, &n_coords);
	g_assert_cmpuint (n_coords, ==, 8);
	g_assert_cmpfloat (poly[1], ==, 8.0f);
	g_assert_cmpstr (lrg_tileset_get_tile_data (ts, 5, "kind"), ==, "water");
	g_assert_cmpfloat (lrg_tileset_get_tile_data_double (ts, 5, "damage", 0.0), ==, 2.0);
	keys = lrg_tileset_get_tile_data_keys (ts, 5);
	g_assert_cmpuint (g_strv_length (keys), ==, 2);
	g_assert_cmpstr (keys[0], ==, "kind");

	g_assert_cmpuint (lrg_tileset_get_tile_animation_length (ts, 5), ==, 2);
	g_assert_true (lrg_tileset_get_tile_animation_frame (ts, 5, 1, &frame, &duration));
	g_assert_cmpuint (frame, ==, 6);
	g_assert_cmpuint (duration, ==, 100);
	g_assert_cmpuint (lrg_tileset_get_animated_tile (ts, 5, 0), ==, 5);
	g_assert_cmpuint (lrg_tileset_get_animated_tile (ts, 5, 250), ==, 6);
	g_assert_cmpuint (lrg_tileset_get_animated_tile (ts, 5, 300), ==, 5);
	g_assert_cmpuint (lrg_tileset_get_animated_tile (ts, 4, 250), ==, 4);

	/* rule tiles: file indices are 0-based, cell values 1-based */
	g_assert_cmpuint (lrg_tileset_get_n_rule_tiles (ts), ==, 1);
	rt = lrg_tileset_get_rule_tile_by_name (ts, "grass");
	g_assert_nonnull (rt);
	g_assert_cmpuint (lrg_rule_tile_get_default_tile (rt), ==, 21);
	g_assert_cmpuint (lrg_rule_tile_get_seed (rt), ==, 7);
	g_assert_cmpuint (lrg_rule_tile_get_n_rules (rt), ==, 2);
	g_assert_true (lrg_rule_tile_matches_tile (rt, 4));
	g_assert_false (lrg_rule_tile_owns_tile (rt, 4));
	g_assert_true (lrg_tileset_find_rule_tile_for (ts, 22) == rt);
	rule = lrg_rule_tile_get_rule (rt, 0);
	g_assert_cmpint (lrg_tile_rule_get_transform (rule), ==, LRG_TILE_RULE_TRANSFORM_ROTATED);
	g_assert_cmpuint (lrg_tile_rule_get_this_mask (rule), ==, LRG_TILE_NEIGHBOR_N);
	g_assert_cmpuint (lrg_tile_rule_get_not_this_mask (rule), ==, LRG_TILE_NEIGHBOR_E | LRG_TILE_NEIGHBOR_W);
	g_assert_cmpuint (lrg_tile_rule_get_output (rule, 1, &weight), ==, lrg_tile_make (23, TRUE, 1));
	g_assert_cmpfloat (weight, ==, 0.5f);

	/* save: the fixture is the writer's own layout, byte for byte */
	text = lrg_tileset_save_to_data (ts, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (text, ==, rtileset_fixture);

	/* and the round trip is stable */
	again = lrg_tileset_new_from_asset_data (text, -1, NULL, NULL, FALSE, &error);
	g_assert_no_error (error);
	text2 = lrg_tileset_save_to_data (again, NULL, &error);
	g_assert_cmpstr (text2, ==, text);
}

static void
check_error (const gchar *text,
             const gchar *expected)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgTileset) ts = NULL;

	ts = lrg_tileset_new_from_asset_data (text, -1, NULL, "bad.rtileset", FALSE, &error);
	g_assert_null (ts);
	g_assert_error (error, LRG_TILESET_ERROR, LRG_TILESET_ERROR_INVALID);
	if (strstr (error->message, expected) == NULL)
		g_error ("expected \"%s\" in \"%s\"", expected, error->message);
}

#define HEAD "tile_size: [16, 16]\ncolumns: 4\ntile_count: 16\n"

static void
test_rtileset_errors (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgTileset) ts = NULL;

	check_error ("columns: 4\ntile_count: 16\n", "bad.rtileset: /: missing key \"tile_size\"");
	check_error ("tile_size: [16]\ncolumns: 4\ntile_count: 4\n", "/tile_size: expected [width, height]");
	check_error (HEAD "tiles:\n- id: 16\n", "/tiles/0/id: expected a whole number from 0 to 15");
	check_error (HEAD "tiles:\n- id: 2\n  flags: [solid, sticky]\n", "/tiles/0/flags/1: expected solid, animated or hazard");
	check_error (HEAD "tiles:\n- id: 2\n  collision:\n  - [0, 0, 4, 4]\n", "/tiles/0/collision/0: a polygon needs at least three");
	check_error (HEAD "tiles:\n- id: 2\n  animation:\n  - {tile: 3, ms: 4}\n", "/tiles/0/animation/0: unknown key \"ms\"");
	check_error (HEAD "tiles:\n- id: 2\n- id: 2\n", "/tiles/1: tile 2 is listed twice");
	check_error (HEAD "rule_tiles:\n- name: a\n  rules:\n  - pattern: [\"...\", \".x.\", \"...\"]\n    tiles: [1]\n",
	             "/rule_tiles/0/rules/0/pattern/1: the centre character must be '@'");
	check_error (HEAD "rule_tiles:\n- name: a\n  rules:\n  - tiles: [99]\n",
	             "/rule_tiles/0/rules/0/tiles/0: expected a whole number from 0 to 15");
	check_error (HEAD "rule_tiles:\n- name: a\n  rules:\n  - transform: spun\n    tiles: [1]\n",
	             "/rule_tiles/0/rules/0/transform: expected fixed, rotated");
	check_error (HEAD "rule_tiles:\n- name: a\n- name: a\n", "/rule_tiles/1: rule tile \"a\" is defined twice");
	check_error ("version: 2\n" HEAD, "/version: expected a whole number from 1 to 1");

	/* broken YAML is a parse error */
	ts = lrg_tileset_new_from_asset_data ("tile_size: [16, 16\n", -1, NULL, NULL, FALSE, &error);
	g_assert_null (ts);
	g_assert_error (error, LRG_TILESET_ERROR, LRG_TILESET_ERROR_PARSE);
}

static void
test_rtileset_edit_and_file (void)
{
	g_autoptr(LrgTileset) ts = lrg_tileset_new_empty (32, 32, 4, 8);
	g_autoptr(LrgRuleTile) rt = lrg_rule_tile_new_edges16 ("road", 1);
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgTileset) loaded = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *text = NULL;
	g_autofree guint *found = NULL;
	guint n_found = 0;
	gfloat tri[6] = { 0, 0, 32, 0, 0, 32 };
	guint frames[2] = { 1, 2 };
	guint durations[2] = { 100, 100 };
	guint changed = 0;

	g_signal_connect_swapped (ts, "changed", G_CALLBACK (g_atomic_int_inc), &changed);

	lrg_tileset_set_name (ts, "edited");
	lrg_tileset_set_tile_properties (ts, 0, LRG_TILE_PROPERTY_SOLID);
	lrg_tileset_add_tile_collision_polygon (ts, 1, tri, 6);
	lrg_tileset_set_tile_data (ts, 1, "sound", "stone");
	lrg_tileset_set_tile_data (ts, 2, "sound", "grass");
	lrg_tileset_set_tile_animation (ts, 3, frames, durations, 2);
	g_assert_cmpuint (changed, ==, 5);

	found = lrg_tileset_find_tiles_with_data (ts, "sound", NULL, &n_found);
	g_assert_cmpuint (n_found, ==, 2);
	g_assert_cmpuint (found[1], ==, 2);

	/* removing data and animation */
	lrg_tileset_set_tile_data (ts, 2, "sound", NULL);
	g_assert_null (lrg_tileset_get_tile_data (ts, 2, "sound"));
	lrg_tileset_set_tile_animation (ts, 3, NULL, NULL, 0);
	g_assert_false (lrg_tileset_tile_has_property (ts, 3, LRG_TILE_PROPERTY_ANIMATED));

	/* the preset's 16 rules reference tiles 0..15 but the set only has 8:
	 * a 16-tile preset needs 16 tiles, so use a fresh range */
	lrg_tileset_add_rule_tile (ts, rt);
	g_assert_cmpuint (lrg_tileset_get_n_rule_tiles (ts), ==, 1);
	g_assert_true (lrg_tileset_remove_rule_tile (ts, rt));

	dir = g_dir_make_tmp ("rtileset-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "edited.rtileset", NULL);
	g_assert_true (lrg_tileset_save_to_file (ts, path, &error));
	g_assert_no_error (error);

	loaded = lrg_tileset_new_from_asset_file (path, FALSE, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (lrg_tileset_get_tile_data (loaded, 1, "sound"), ==, "stone");
	g_assert_cmpuint (lrg_tileset_get_tile_collision_polygon_count (loaded, 1), ==, 1);
	text = lrg_tileset_save_to_data (loaded, NULL, &error);
	g_assert_cmpstr (text, ==,
	                 "version: 1\n"
	                 "name: edited\n"
	                 "tile_size:\n- 32\n- 32\n"
	                 "columns: 4\n"
	                 "tile_count: 8\n"
	                 "tiles:\n"
	                 "- id: 0\n  flags:\n  - solid\n"
	                 "- id: 1\n  collision:\n  - - 0\n    - 0\n    - 32\n    - 0\n    - 0\n    - 32\n"
	                 "  data:\n    sound: stone\n");

	/* a texture load without a window fails cleanly */
	g_assert_false (lrg_tileset_load_texture (loaded, &error));
	g_assert_error (error, LRG_TILESET_ERROR, LRG_TILESET_ERROR_IO);

	g_unlink (path);
	g_rmdir (dir);
}

/* ==========================================================================
 * Isometric, staggered and hexagonal maps
 * ========================================================================== */

static LrgTilemap *
new_map (guint                 w,
         guint                 h,
         guint                 cell_w,
         guint                 cell_h,
         LrgTilemapOrientation orientation)
{
	g_autoptr(LrgTileset) ts = lrg_tileset_new_empty (cell_w, cell_h, 4, 16);
	g_autoptr(LrgTilemapLayer) layer = lrg_tilemap_layer_new (w, h);
	LrgTilemap *map = lrg_tilemap_new (ts);

	lrg_tilemap_add_layer (map, layer);
	lrg_tilemap_set_orientation (map, orientation);
	return map;
}

/* every cell's centre picks back to the cell, and its outline contains it */
static void
check_picking (LrgTilemap *map)
{
	guint w = lrg_tilemap_get_width (map);
	guint h = lrg_tilemap_get_height (map);
	guint x;
	guint y;

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			gfloat cx;
			gfloat cy;
			gint px;
			gint py;

			lrg_tilemap_cell_to_world (map, (gint) x, (gint) y, &cx, &cy);
			g_assert_true (lrg_tilemap_world_to_cell (map, cx, cy, &px, &py));
			g_assert_cmpint (px, ==, (gint) x);
			g_assert_cmpint (py, ==, (gint) y);
			/* slightly off-centre too */
			g_assert_true (lrg_tilemap_world_to_cell (map, cx + 2.0f, cy - 1.5f, &px, &py));
			g_assert_cmpint (px, ==, (gint) x);
			g_assert_cmpint (py, ==, (gint) y);
		}
	}
}

static void
test_isometric (void)
{
	g_autoptr(LrgTilemap) map = new_map (4, 3, 64, 32, LRG_TILEMAP_ORIENTATION_ISOMETRIC);
	g_autoptr(GrlRectangle) bounds = NULL;
	g_autoptr(GArray) order = NULL;
	GrlRectangle box;
	gfloat poly[12];
	gfloat cx;
	gfloat cy;
	gint px;
	gint py;

	/* the top corner of (0, 0) sits at map_height * cw / 2 */
	lrg_tilemap_cell_to_world (map, 0, 0, &cx, &cy);
	g_assert_cmpfloat (cx, ==, 96.0f);
	g_assert_cmpfloat (cy, ==, 16.0f);
	lrg_tilemap_cell_to_world (map, 1, 0, &cx, &cy);
	g_assert_cmpfloat (cx, ==, 128.0f);
	g_assert_cmpfloat (cy, ==, 32.0f);
	lrg_tilemap_cell_to_world (map, 0, 1, &cx, &cy);
	g_assert_cmpfloat (cx, ==, 64.0f);
	g_assert_cmpfloat (cy, ==, 32.0f);

	/* the map's bounding box starts at 0 */
	bounds = lrg_tilemap_get_world_bounds (map);
	g_assert_cmpfloat (bounds->width, ==, 224.0f);
	g_assert_cmpfloat (bounds->height, ==, 112.0f);
	lrg_tilemap_get_cell_bounds (map, 0, 2, &box);
	g_assert_cmpfloat (box.x, ==, 0.0f);

	/* outline: a diamond, clockwise from the top */
	g_assert_cmpuint (lrg_tilemap_get_cell_polygon (map, 0, 0, poly), ==, 4);
	g_assert_cmpfloat (poly[0], ==, 96.0f);
	g_assert_cmpfloat (poly[1], ==, 0.0f);
	g_assert_cmpfloat (poly[2], ==, 128.0f);
	g_assert_cmpfloat (poly[3], ==, 16.0f);

	check_picking (map);

	/* exact picking: the top-left bounding-box corner of (0, 0) lies in
	 * the diamond of (-1, 0), outside the map */
	g_assert_false (lrg_tilemap_world_to_cell (map, 66.0f, 2.0f, &px, &py));
	g_assert_cmpint (px, ==, -1);
	g_assert_cmpint (py, ==, 0);

	/* the legacy accessors follow the orientation */
	{
		guint tx;
		guint ty;

		lrg_tilemap_world_to_tile (map, 128.0f, 32.0f, &tx, &ty);
		g_assert_cmpuint (tx, ==, 1);
		g_assert_cmpuint (ty, ==, 0);
	}

	/* draw order: row by row, so (x, y) comes after (x-1, y) and (x, y-1) */
	order = lrg_tilemap_get_draw_order (map, 0, 0, 4, 3);
	g_assert_cmpuint (order->len, ==, 24);
	g_assert_cmpint (g_array_index (order, gint, 2), ==, 1);
	g_assert_cmpint (g_array_index (order, gint, 3), ==, 0);
	g_assert_cmpfloat (lrg_tilemap_get_cell_depth (map, 1, 1), >, lrg_tilemap_get_cell_depth (map, 1, 0));
	g_assert_cmpint (lrg_tilemap_get_neighbor_layout (map), ==, LRG_TILE_NEIGHBOR_LAYOUT_SQUARE);
}

static void
test_hexagonal (void)
{
	g_autoptr(LrgTilemap) rows = new_map (5, 4, 32, 32, LRG_TILEMAP_ORIENTATION_HEXAGONAL);
	g_autoptr(LrgTilemap) cols = new_map (5, 4, 32, 32, LRG_TILEMAP_ORIENTATION_HEXAGONAL);
	g_autoptr(GArray) order = NULL;
	GrlRectangle box;
	gfloat poly[12];
	gint px;
	gint py;

	/* pointy-top rows, odd rows shifted: side 16, row step 24 */
	lrg_tilemap_get_cell_bounds (rows, 0, 1, &box);
	g_assert_cmpfloat (box.x, ==, 16.0f);
	g_assert_cmpfloat (box.y, ==, 24.0f);
	g_assert_cmpuint (lrg_tilemap_get_cell_polygon (rows, 0, 0, poly), ==, 6);
	g_assert_cmpfloat (poly[3], ==, 8.0f);      /* right corner starts at side offset */
	g_assert_cmpfloat (poly[5], ==, 24.0f);
	check_picking (rows);

	/* just below the pointy top of (1, 1) at (64, 24): inside (1, 1), even
	 * though the point is inside the bounding boxes of row 0 too; just
	 * above the bottom tip of (1, 0) at (48, 32): inside (1, 0) */
	g_assert_true (lrg_tilemap_world_to_cell (rows, 64.0f, 26.0f, &px, &py));
	g_assert_cmpint (px, ==, 1);
	g_assert_cmpint (py, ==, 1);
	g_assert_true (lrg_tilemap_world_to_cell (rows, 48.0f, 30.0f, &px, &py));
	g_assert_cmpint (px, ==, 1);
	g_assert_cmpint (py, ==, 0);
	g_assert_cmpint (lrg_tilemap_get_neighbor_layout (rows), ==, LRG_TILE_NEIGHBOR_LAYOUT_HEX_ROWS_ODD);

	/* an explicit side length */
	lrg_tilemap_set_hex_side_length (rows, 8);
	lrg_tilemap_get_cell_bounds (rows, 0, 1, &box);
	g_assert_cmpfloat (box.y, ==, 20.0f);
	check_picking (rows);

	/* flat-top columns, even columns shifted down */
	lrg_tilemap_set_stagger_axis (cols, LRG_TILEMAP_STAGGER_AXIS_X);
	lrg_tilemap_set_stagger_index (cols, LRG_TILEMAP_STAGGER_INDEX_EVEN);
	lrg_tilemap_get_cell_bounds (cols, 0, 0, &box);
	g_assert_cmpfloat (box.y, ==, 16.0f);
	lrg_tilemap_get_cell_bounds (cols, 1, 0, &box);
	g_assert_cmpfloat (box.x, ==, 24.0f);
	g_assert_cmpfloat (box.y, ==, 0.0f);
	check_picking (cols);
	g_assert_cmpint (lrg_tilemap_get_neighbor_layout (cols), ==, LRG_TILE_NEIGHBOR_LAYOUT_HEX_COLUMNS_EVEN);

	/* column-staggered draw order: raised (odd here) columns first per row */
	order = lrg_tilemap_get_draw_order (cols, 0, 0, 4, 1);
	g_assert_cmpint (g_array_index (order, gint, 0), ==, 1);
	g_assert_cmpint (g_array_index (order, gint, 2), ==, 3);
	g_assert_cmpint (g_array_index (order, gint, 4), ==, 0);
	g_assert_cmpint (g_array_index (order, gint, 6), ==, 2);
}

static void
test_staggered (void)
{
	g_autoptr(LrgTilemap) map = new_map (4, 6, 64, 32, LRG_TILEMAP_ORIENTATION_STAGGERED);
	GrlRectangle box;
	gfloat poly[12];

	/* rows advance half a cell; odd rows shift half a cell right */
	lrg_tilemap_get_cell_bounds (map, 0, 1, &box);
	g_assert_cmpfloat (box.x, ==, 32.0f);
	g_assert_cmpfloat (box.y, ==, 16.0f);
	g_assert_cmpuint (lrg_tilemap_get_cell_polygon (map, 0, 0, poly), ==, 4);
	check_picking (map);
	g_assert_cmpint (lrg_tilemap_get_neighbor_layout (map), ==, LRG_TILE_NEIGHBOR_LAYOUT_STAGGERED_ODD);
}

static void
count_changes (LrgTilemap *map,
               guint       layer,
               guint       x,
               guint       y,
               guint       old_tile,
               guint       new_tile,
               gpointer    data)
{
	(void) map; (void) layer; (void) x; (void) y; (void) old_tile; (void) new_tile;
	(*(guint *) data)++;
}

static void
test_tilemap_rule_painting (void)
{
	g_autoptr(LrgTileset) ts = lrg_tileset_new_empty (16, 16, 8, 64);
	g_autoptr(LrgRuleTile) rt = lrg_rule_tile_new_blob47 ("ground", 1);
	g_autoptr(LrgTilemapLayer) layer = lrg_tilemap_layer_new (6, 6);
	g_autoptr(LrgTilemap) map = NULL;
	guint signals = 0;
	guint changed;

	lrg_tileset_add_rule_tile (ts, rt);
	map = lrg_tilemap_new (ts);
	lrg_tilemap_add_layer (map, layer);
	g_signal_connect (map, "tile-changed", G_CALLBACK (count_changes), &signals);

	changed = lrg_tilemap_paint_rule_tile (map, 0, rt, 2, 2);
	g_assert_cmpuint (changed, ==, 1);
	changed = lrg_tilemap_paint_rule_tile (map, 0, rt, 3, 2);
	g_assert_cmpuint (changed, ==, 2);   /* the new cell and its neighbour */
	g_assert_cmpuint (signals, ==, 3);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 2, 2), ==,
	                  1 + lrg_rule_tile_blob47_rank (LRG_TILE_NEIGHBOR_E));

	/* a cell painted by hand with any owned tile is fixed by a refresh */
	lrg_tilemap_layer_set_tile (layer, 4, 2, 1);
	g_assert_cmpuint (lrg_tilemap_refresh_rule_tiles (map, 0), ==, 2);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 3, 2), ==,
	                  1 + lrg_rule_tile_blob47_rank (LRG_TILE_NEIGHBOR_E | LRG_TILE_NEIGHBOR_W));

	/* erase through the tileset's rule tiles */
	lrg_tilemap_erase_rule_tile (map, 0, 3, 2);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 3, 2), ==, 0);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 2, 2), ==, 1);

	/* flags in cells: is_solid ignores them */
	lrg_tileset_set_tile_properties (ts, 0, LRG_TILE_PROPERTY_SOLID);
	lrg_tilemap_layer_set_collision_enabled (layer, TRUE);
	lrg_tilemap_layer_set_tile (layer, 0, 0, lrg_tile_make (1, TRUE, 3));
	g_assert_true (lrg_tilemap_is_solid (map, 0, 0));
}

static void
test_tilemap_animation_clock (void)
{
	g_autoptr(LrgTilemap) map = new_map (2, 2, 16, 16, LRG_TILEMAP_ORIENTATION_ORTHOGONAL);

	/* 1/128 s = 7.8125 ms is exact in binary: the sub-millisecond rest
	 * carries over, 4 x 7.8125 = 31.25 */
	lrg_tilemap_update (map, 0.0078125f);
	lrg_tilemap_update (map, 0.0078125f);
	lrg_tilemap_update (map, 0.0078125f);
	lrg_tilemap_update (map, 0.0078125f);
	g_assert_cmpuint (lrg_tilemap_get_animation_time (map), ==, 31);
	lrg_tilemap_set_animation_time (map, 5000);
	g_assert_cmpuint (lrg_tilemap_get_animation_time (map), ==, 5000);
}

/* ==========================================================================
 * Shadow occluders from tilemaps
 * ========================================================================== */

static void
test_occluder_from_tilemap (void)
{
	g_autoptr(LrgTileset) ts = lrg_tileset_new_empty (16, 16, 4, 16);
	g_autoptr(LrgTilemapLayer) layer = lrg_tilemap_layer_new (5, 5);
	g_autoptr(LrgTilemap) map = NULL;
	g_autoptr(LrgShadowOccluder2D) occ = lrg_shadow_occluder2d_new ();
	gfloat half[8] = { 0, 8, 16, 8, 16, 16, 0, 16 };
	LrgShadowEdge edge;
	guint i;

	lrg_tileset_set_tile_properties (ts, 0, LRG_TILE_PROPERTY_SOLID);
	lrg_tileset_add_tile_collision_polygon (ts, 1, half, 8);
	map = lrg_tilemap_new (ts);
	lrg_tilemap_add_layer (map, layer);
	lrg_tilemap_layer_set_collision_enabled (layer, TRUE);

	/* a 3-tile wall: shared sides cancel, 8 unit edges remain */
	lrg_tilemap_layer_set_tile (layer, 1, 1, 1);
	lrg_tilemap_layer_set_tile (layer, 2, 1, 1);
	lrg_tilemap_layer_set_tile (layer, 3, 1, 1);
	g_assert_cmpuint (lrg_shadow_occluder2d_add_tilemap (occ, map, -1, 100.0f, 0.0f), ==, 3);
	g_assert_cmpuint (lrg_shadow_occluder2d_get_edge_count (occ), ==, 8);

	/* edges run clockwise: the top edges go left to right */
	for (i = 0; i < lrg_shadow_occluder2d_get_edge_count (occ); i++)
	{
		g_assert_true (lrg_shadow_occluder2d_get_edge (occ, i, &edge));
		if (edge.y1 == 16.0f && edge.y2 == 16.0f)
			g_assert_cmpfloat (edge.x2, >, edge.x1);
		g_assert_cmpfloat (edge.x1, >=, 116.0f);
	}

	/* a half-height tile rotated a quarter turn: its polygon follows */
	lrg_shadow_occluder2d_clear (occ);
	lrg_tilemap_layer_clear (layer);
	lrg_tilemap_layer_set_tile (layer, 0, 0, lrg_tile_make (2, FALSE, 1));
	lrg_shadow_occluder2d_add_tilemap (occ, map, 0, 0.0f, 0.0f);
	g_assert_cmpuint (lrg_shadow_occluder2d_get_edge_count (occ), ==, 4);
	for (i = 0; i < 4; i++)
	{
		lrg_shadow_occluder2d_get_edge (occ, i, &edge);
		/* the bottom half turned clockwise is the left half */
		g_assert_cmpfloat (edge.x1, <=, 8.0f);
		g_assert_cmpfloat (edge.x2, <=, 8.0f);
	}

	/* isometric cells contribute diamonds */
	lrg_shadow_occluder2d_clear (occ);
	lrg_tilemap_layer_clear (layer);
	lrg_tilemap_set_orientation (map, LRG_TILEMAP_ORIENTATION_ISOMETRIC);
	lrg_tilemap_layer_set_tile (layer, 0, 0, 1);
	lrg_tilemap_layer_set_tile (layer, 1, 0, 1);
	lrg_shadow_occluder2d_add_tilemap (occ, map, 0, 0.0f, 0.0f);
	g_assert_cmpuint (lrg_shadow_occluder2d_get_edge_count (occ), ==, 6);
}

/* ==========================================================================
 * GPU: isometric draw order and flips (skipped without a display)
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
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	test_window = grl_window_new (8, 8, "lrg-rule-tile-test");
	if (test_window == NULL || !grl_window_is_ready (test_window))
	{
		g_clear_object (&test_window);
		return FALSE;
	}
	return TRUE;
}

/* a 64x32 tileset image: tile 1 red, tile 2 green (left half dark green) */
static GrlTexture *
two_tile_texture (void)
{
	g_autofree guint8 *px = g_new0 (guint8, 64 * 32 * 4);
	g_autoptr(GrlImage) image = NULL;
	gint x;
	gint y;

	for (y = 0; y < 32; y++)
	{
		for (x = 0; x < 64; x++)
		{
			guint8 *p = px + (y * 64 + x) * 4;

			if (x < 32)
				p[0] = 255;
			else
				p[1] = x < 48 ? 100 : 255;
			p[3] = 255;
		}
	}
	image = grl_image_new_from_pixels (64, 32, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, px);
	return grl_texture_new_from_image (image);
}

static void
pixel (GrlImage *image,
       gint      x,
       gint      y,
       guint8   *r,
       guint8   *g)
{
	guint8 *p = grl_image_get_pixels (image, NULL) + (y * grl_image_get_width (image) + x) * 4;

	*r = p[0];
	*g = p[1];
}

static void
test_gpu_isometric_order (void)
{
	g_autoptr(GrlTexture) texture = NULL;
	g_autoptr(LrgTileset) ts = NULL;
	g_autoptr(LrgTilemapLayer) layer = lrg_tilemap_layer_new (2, 2);
	g_autoptr(LrgTilemap) map = NULL;
	g_autoptr(GrlRenderTexture) target = NULL;
	g_autoptr(GrlColor) black = grl_color_new (0, 0, 0, 255);
	g_autoptr(GrlTexture) result = NULL;
	g_autoptr(GrlImage) image = NULL;
	guint8 r40;
	guint8 g40;
	guint8 r23;
	guint8 g23;

	if (!graphics_available)
	{
		g_test_skip ("No display available (headless environment)");
		return;
	}

	/* 32x32 tiles on a 32x16 isometric grid: tiles stick out 16 px up */
	texture = two_tile_texture ();
	ts = lrg_tileset_new (texture, 32, 32);
	map = lrg_tilemap_new (ts);
	lrg_tilemap_add_layer (map, layer);
	lrg_tilemap_set_orientation (map, LRG_TILEMAP_ORIENTATION_ISOMETRIC);
	lrg_tilemap_set_cell_size (map, 32, 16);
	lrg_tilemap_layer_set_tile (layer, 0, 0, 1);
	lrg_tilemap_layer_set_tile (layer, 1, 1, 2);

	/*
	 * Drawn at (0, 32): the image of (0, 0) covers y 16..48, the image of
	 * (1, 1) (in front, drawn later) y 32..64, both x 16..48.  In the
	 * overlap (y 32..48) green must win; above it (y 16..32) red shows.
	 * Render textures read back upside down, so check y 40 and its mirror
	 * 23 and accept either orientation.
	 */
	target = grl_render_texture_new (64, 64);
	grl_render_texture_begin (target);
	grl_draw_clear_background (black);
	lrg_tilemap_draw_at (map, 0.0f, 32.0f);
	grl_render_texture_end (target);

	result = grl_render_texture_get_texture (target);
	image = grl_texture_to_image (result);
	pixel (image, 40, 40, &r40, &g40);
	pixel (image, 40, 23, &r23, &g23);
	g_assert_true ((g40 > 200 && r23 > 200) || (r40 > 200 && g23 > 200));

	/* a mirrored tile 2 shows its bright half on the left */
	lrg_tilemap_layer_set_tile (layer, 1, 1, lrg_tile_make (2, TRUE, 0));
	lrg_tilemap_layer_set_tile (layer, 0, 0, 0);
	grl_render_texture_begin (target);
	grl_draw_clear_background (black);
	lrg_tilemap_draw_at (map, 0.0f, 32.0f);
	grl_render_texture_end (target);
	g_clear_object (&image);
	image = grl_texture_to_image (result);
	pixel (image, 20, 48, &r40, &g40);
	pixel (image, 44, 48, &r23, &g23);
	if (g40 == 0)
	{
		/* upside down: sample the mirrored rows */
		pixel (image, 20, 15, &r40, &g40);
		pixel (image, 44, 15, &r23, &g23);
	}
	g_assert_cmpuint (g40, ==, 255);
	g_assert_cmpuint (g23, ==, 100);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	graphics_available = init_graphics_context ();

	g_test_add_func ("/tile/transform/bits", test_transform_bits);
	g_test_add_func ("/tile/transform/point", test_transform_point);
	g_test_add_func ("/tile/transform/layer", test_layer_transform);

	g_test_add_func ("/rule-tile/edges16", test_edges16);
	g_test_add_func ("/rule-tile/blob47", test_blob47);
	g_test_add_func ("/rule-tile/variants", test_variants);
	g_test_add_func ("/rule-tile/rotated-mirrored", test_rotated_and_mirrored);
	g_test_add_func ("/rule-tile/neighbors", test_neighbors);
	g_test_add_func ("/rule-tile/hex", test_hex_rule_tile);

	g_test_add_func ("/rtileset/round-trip", test_rtileset_round_trip);
	g_test_add_func ("/rtileset/errors", test_rtileset_errors);
	g_test_add_func ("/rtileset/edit-and-file", test_rtileset_edit_and_file);

	g_test_add_func ("/tilemap/isometric", test_isometric);
	g_test_add_func ("/tilemap/hexagonal", test_hexagonal);
	g_test_add_func ("/tilemap/staggered", test_staggered);
	g_test_add_func ("/tilemap/rule-painting", test_tilemap_rule_painting);
	g_test_add_func ("/tilemap/animation-clock", test_tilemap_animation_clock);

	g_test_add_func ("/shadow-occluder/tilemap", test_occluder_from_tilemap);
	g_test_add_func ("/gpu/isometric-order", test_gpu_isometric_order);

	result = g_test_run ();
	g_clear_object (&test_window);
	return result;
}
