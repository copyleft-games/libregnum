/* test-procgen.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the procedural generation module: grid passes, every
 * generator (determinism through golden hashes, invariants), wave function
 * collapse rules and contradictions, tile rules, navigation and tilemap
 * conversion, YAML recipes and ECS spawning.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <libregnum.h>

/*
 * Golden hashes (lrg_procgen_grid_hash) of fixed seeds. Generation is
 * integer-only, so these must match on every platform; a change here is a
 * change of every map a game has ever shipped for that seed.
 */
#define GOLDEN_BSP       G_GUINT64_CONSTANT (0xb7c5c4a3bc2952bc)
#define GOLDEN_CAVES     G_GUINT64_CONSTANT (0x8d83c61108bad347)
#define GOLDEN_DRUNKARD  G_GUINT64_CONSTANT (0x18ab6b627a84d14c)
#define GOLDEN_NOISE     G_GUINT64_CONSTANT (0x1b10afc4e0fcf395)
#define GOLDEN_WFC       G_GUINT64_CONSTANT (0xf3df8360e666b0fb)
#define GOLDEN_DUNGEON   G_GUINT64_CONSTANT (0x3e8883e70ed1e137)

/* a small sample for wave function collapse: rooms, corridors, doors */
static const gchar *wfc_sample =
	"##########\n"
	"#....#...#\n"
	"#....+...#\n"
	"#....#...#\n"
	"##+###.###\n"
	"#,,,,#...#\n"
	"#,##,#...#\n"
	"##########\n";

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* every border cell is a wall */
static void
assert_border_walls (LrgProcgenGrid *grid)
{
	gint w = (gint) lrg_procgen_grid_get_width (grid);
	gint h = (gint) lrg_procgen_grid_get_height (grid);
	gint x;
	gint y;

	for (x = 0; x < w; x++)
	{
		g_assert_cmpuint (lrg_procgen_grid_get_cell (grid, x, 0), ==, LRG_PROCGEN_CELL_WALL);
		g_assert_cmpuint (lrg_procgen_grid_get_cell (grid, x, h - 1), ==, LRG_PROCGEN_CELL_WALL);
	}
	for (y = 0; y < h; y++)
	{
		g_assert_cmpuint (lrg_procgen_grid_get_cell (grid, 0, y), ==, LRG_PROCGEN_CELL_WALL);
		g_assert_cmpuint (lrg_procgen_grid_get_cell (grid, w - 1, y), ==, LRG_PROCGEN_CELL_WALL);
	}
}

/* prints a hash so a golden value can be (re)recorded with --verbose */
static void
check_golden (const gchar *what,
              guint64      hash,
              guint64      golden)
{
	g_test_message ("%s hash: 0x%016" G_GINT64_MODIFIER "x", what, hash);
	if (golden != 0)
		g_assert_cmpuint (hash, ==, golden);
}

/* ==========================================================================
 * Grid
 * ========================================================================== */

static void
test_grid_basics (void)
{
	g_autoptr(LrgProcgenGrid) grid = lrg_procgen_grid_new (8, 5);
	g_autoptr(LrgProcgenGrid) parsed = NULL;
	g_autoptr(LrgProcgenGrid) copy = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *text = NULL;

	g_assert_cmpuint (lrg_procgen_grid_get_width (grid), ==, 8);
	g_assert_cmpuint (lrg_procgen_grid_get_height (grid), ==, 5);
	g_assert_cmpuint (lrg_procgen_grid_count_cells (grid, LRG_PROCGEN_CELL_WALL), ==, 40);

	lrg_procgen_grid_fill_rect (grid, 1, 1, 3, 2, LRG_PROCGEN_CELL_FLOOR);
	lrg_procgen_grid_set_cell (grid, 4, 1, LRG_PROCGEN_CELL_DOOR);
	lrg_procgen_grid_set_cell (grid, 5, 1, LRG_PROCGEN_CELL_CORRIDOR);
	lrg_procgen_grid_set_cell (grid, 6, 3, LRG_PROCGEN_CELL_WATER);
	lrg_procgen_grid_set_cell (grid, 6, 2, LRG_PROCGEN_CELL_CUSTOM + 2);
	lrg_procgen_grid_set_cell (grid, 99, 99, LRG_PROCGEN_CELL_FLOOR); /* ignored */

	/* out of bounds reads as wall */
	g_assert_cmpuint (lrg_procgen_grid_get_cell (grid, -1, 0), ==, LRG_PROCGEN_CELL_WALL);
	g_assert_false (lrg_procgen_grid_in_bounds (grid, 8, 0));
	g_assert_true (lrg_procgen_grid_is_walkable (grid, 4, 1));
	g_assert_false (lrg_procgen_grid_is_walkable (grid, 6, 3));
	g_assert_cmpuint (lrg_procgen_grid_count_walkable (grid), ==, 8);

	/* text round trip */
	text = lrg_procgen_grid_to_string (grid);
	g_assert_cmpstr (text, ==,
	                 "########\n"
	                 "#...+,##\n"
	                 "#...##c#\n"
	                 "######~#\n"
	                 "########\n");
	parsed = lrg_procgen_grid_new_from_string (text, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_procgen_grid_hash (parsed), ==, lrg_procgen_grid_hash (grid));

	/* walkability is per grid and per tag */
	lrg_procgen_grid_set_tag_walkable (grid, LRG_PROCGEN_CELL_WATER, TRUE);
	g_assert_true (lrg_procgen_grid_is_walkable (grid, 6, 3));

	copy = lrg_procgen_grid_copy (grid);
	g_assert_cmpuint (lrg_procgen_grid_hash (copy), ==, lrg_procgen_grid_hash (grid));
	g_assert_true (lrg_procgen_grid_is_tag_walkable (copy, LRG_PROCGEN_CELL_WATER));

	/* malformed text */
	g_assert_null (lrg_procgen_grid_new_from_string ("##\n#\n", &error));
	g_assert_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG);
	g_clear_error (&error);
	g_assert_null (lrg_procgen_grid_new_from_string ("#!#\n", &error));
	g_assert_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG);
}

static void
test_grid_regions (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgProcgenGrid) grid = lrg_procgen_grid_new_from_string (
		"############\n"
		"#...##.....#\n"
		"#...##.....#\n"
		"######.....#\n"
		"#.#######..#\n"
		"############\n", &error);

	g_assert_no_error (error);
	g_assert_cmpuint (lrg_procgen_grid_label_regions (grid), ==, 3);
	g_assert_cmpint (lrg_procgen_grid_get_region (grid, 1, 1), ==, 0);
	g_assert_cmpint (lrg_procgen_grid_get_region (grid, 6, 1), ==, 1);
	g_assert_cmpint (lrg_procgen_grid_get_region (grid, 1, 4), ==, 2);
	g_assert_cmpint (lrg_procgen_grid_get_region (grid, 0, 0), ==, -1);
	g_assert_cmpuint (lrg_procgen_grid_get_region_size (grid, 0), ==, 6);
	g_assert_cmpuint (lrg_procgen_grid_get_region_size (grid, 1), ==, 17);
	g_assert_cmpuint (lrg_procgen_grid_get_region_size (grid, 2), ==, 1);
	g_assert_false (lrg_procgen_grid_is_connected (grid));
	g_assert_cmpint (lrg_procgen_grid_path_distance (grid, 1, 1, 6, 1), ==, -1);

	/* the single cell goes, the rest gets joined by one corridor */
	g_assert_cmpuint (lrg_procgen_grid_cull_regions (grid, 2, LRG_PROCGEN_CELL_WALL), ==, 1);
	g_assert_cmpuint (lrg_procgen_grid_get_region_count (grid), ==, 2);
	g_assert_cmpuint (lrg_procgen_grid_connect_regions (grid, LRG_PROCGEN_CELL_CORRIDOR), ==, 1);
	g_assert_true (lrg_procgen_grid_is_connected (grid));
	g_assert_cmpuint (lrg_procgen_grid_count_cells (grid, LRG_PROCGEN_CELL_CORRIDOR), ==, 2);
	assert_border_walls (grid);
	g_assert_cmpint (lrg_procgen_grid_path_distance (grid, 1, 1, 10, 4), ==, 12);

	/* keep-largest leaves one region */
	lrg_procgen_grid_fill_rect (grid, 4, 1, 2, 2, LRG_PROCGEN_CELL_WALL);
	g_assert_cmpuint (lrg_procgen_grid_keep_largest_region (grid, LRG_PROCGEN_CELL_WALL), ==, 1);
	g_assert_cmpuint (lrg_procgen_grid_count_walkable (grid), ==, 17);
}

/* ==========================================================================
 * Reference model: the original flood fills
 *
 * Region labelling, corridor carving and distance maps were rewritten for
 * speed (run-based union-find labelling, division-free searches). The
 * functions below are the original breadth-first implementations over a
 * plain cell array; the fuzz test checks that the grid produces the same
 * labels, sizes, corridors and distances on random maps.
 * ========================================================================== */

typedef struct
{
	gint     w;
	gint     h;
	guint8  *cells;
	gboolean walkable[256];
	gint    *regions;
	GArray  *sizes;
} RefGrid;

static const gint ref_dx[4] = { 0, 1, 0, -1 };
static const gint ref_dy[4] = { -1, 0, 1, 0 };

static gboolean
ref_walkable_at (RefGrid *g,
                 gint     x,
                 gint     y)
{
	return x >= 0 && y >= 0 && x < g->w && y < g->h && g->walkable[g->cells[y * g->w + x]];
}

static guint
ref_label (RefGrid *g)
{
	gsize n = (gsize) g->w * g->h;
	g_autofree guint32 *queue = g_new (guint32, n);
	gsize start;

	g_array_set_size (g->sizes, 0);
	memset (g->regions, 0xff, n * sizeof (gint));
	for (start = 0; start < n; start++)
	{
		gsize head = 0;
		gsize tail = 0;
		gint region;
		guint size = 0;

		if (g->regions[start] >= 0 || !g->walkable[g->cells[start]])
			continue;
		region = (gint) g->sizes->len;
		g->regions[start] = region;
		queue[tail++] = (guint32) start;
		while (head < tail)
		{
			guint32 c = queue[head++];
			gint cx = (gint) (c % g->w);
			gint cy = (gint) (c / g->w);
			guint d;

			size++;
			for (d = 0; d < 4; d++)
			{
				gint nx = cx + ref_dx[d];
				gint ny = cy + ref_dy[d];
				gsize ni;

				if (!ref_walkable_at (g, nx, ny))
					continue;
				ni = (gsize) ny * g->w + nx;
				if (g->regions[ni] >= 0)
					continue;
				g->regions[ni] = region;
				queue[tail++] = (guint32) ni;
			}
		}
		g_array_append_val (g->sizes, size);
	}
	return g->sizes->len;
}

static guint
ref_connect (RefGrid *g,
             guint8   corridor_tag)
{
	gsize n = (gsize) g->w * g->h;
	g_autofree guint32 *queue = g_new (guint32, n);
	g_autofree gint32 *parent = g_new (gint32, n);
	guint carved = 0;

	while (ref_label (g) > 1)
	{
		gsize head = 0;
		gsize tail = 0;
		gsize i;
		gint32 hit = -1;

		for (i = 0; i < n; i++)
		{
			parent[i] = -2;
			if (g->regions[i] == 0)
			{
				parent[i] = -1;
				queue[tail++] = (guint32) i;
			}
		}
		while (head < tail && hit < 0)
		{
			guint32 c = queue[head++];
			gint cx = (gint) (c % g->w);
			gint cy = (gint) (c / g->w);
			guint d;

			for (d = 0; d < 4; d++)
			{
				gint nx = cx + ref_dx[d];
				gint ny = cy + ref_dy[d];
				gsize ni;

				if (nx < 0 || ny < 0 || nx >= g->w || ny >= g->h)
					continue;
				ni = (gsize) ny * g->w + nx;
				if (parent[ni] != -2)
					continue;
				if ((nx == 0 || ny == 0 || nx == g->w - 1 || ny == g->h - 1) && g->regions[ni] < 0)
					continue;
				parent[ni] = (gint32) c;
				if (g->regions[ni] > 0)
				{
					hit = (gint32) ni;
					break;
				}
				queue[tail++] = (guint32) ni;
			}
		}
		if (hit < 0)
			break;
		for (i = (gsize) parent[hit]; parent[i] >= 0; i = (gsize) parent[i])
			if (!g->walkable[g->cells[i]])
				g->cells[i] = corridor_tag;
		carved++;
		if (!g->walkable[corridor_tag])
			break;
	}
	return carved;
}

static void
ref_distances (RefGrid *g,
               gint     from_x,
               gint     from_y,
               gint    *dist)
{
	gsize n = (gsize) g->w * g->h;
	g_autofree guint32 *queue = g_new (guint32, n);
	gsize head = 0;
	gsize tail = 0;
	gsize i;

	for (i = 0; i < n; i++)
		dist[i] = -1;
	if (!ref_walkable_at (g, from_x, from_y))
		return;
	i = (gsize) from_y * g->w + from_x;
	dist[i] = 0;
	queue[tail++] = (guint32) i;
	while (head < tail)
	{
		guint32 c = queue[head++];
		gint cx = (gint) (c % g->w);
		gint cy = (gint) (c / g->w);
		guint d;

		for (d = 0; d < 4; d++)
		{
			gint nx = cx + ref_dx[d];
			gint ny = cy + ref_dy[d];
			gsize ni;

			if (!ref_walkable_at (g, nx, ny))
				continue;
			ni = (gsize) ny * g->w + nx;
			if (dist[ni] >= 0)
				continue;
			dist[ni] = dist[c] + 1;
			queue[tail++] = (guint32) ni;
		}
	}
}

/* a reference copy of @grid's cells and walkable flags */
static RefGrid *
ref_grid_new (LrgProcgenGrid *grid)
{
	RefGrid *g = g_new0 (RefGrid, 1);
	const guint8 *cells;
	gsize n;
	guint t;

	g->w = (gint) lrg_procgen_grid_get_width (grid);
	g->h = (gint) lrg_procgen_grid_get_height (grid);
	cells = lrg_procgen_grid_get_cells (grid, &n);
	g->cells = g_memdup2 (cells, n);
	for (t = 0; t < 256; t++)
		g->walkable[t] = lrg_procgen_grid_is_tag_walkable (grid, (guint8) t);
	g->regions = g_new (gint, n);
	g->sizes = g_array_new (FALSE, TRUE, sizeof (guint));
	return g;
}

static void
ref_grid_free (RefGrid *g)
{
	g_free (g->cells);
	g_free (g->regions);
	g_array_unref (g->sizes);
	g_free (g);
}

/* the grid's cells, labels and sizes equal the reference's */
static void
assert_matches_ref (LrgProcgenGrid *grid,
                    RefGrid        *g)
{
	gsize n;
	const guint8 *cells = lrg_procgen_grid_get_cells (grid, &n);
	gint x;
	gint y;
	guint r;

	g_assert_cmpmem (cells, n, g->cells, (gsize) g->w * g->h);
	g_assert_cmpuint (lrg_procgen_grid_get_region_count (grid), ==, g->sizes->len);
	for (r = 0; r < g->sizes->len; r++)
		g_assert_cmpuint (lrg_procgen_grid_get_region_size (grid, r), ==,
		                  g_array_index (g->sizes, guint, r));
	for (y = 0; y < g->h; y++)
		for (x = 0; x < g->w; x++)
			g_assert_cmpint (lrg_procgen_grid_get_region (grid, x, y), ==, g->regions[y * g->w + x]);
}

static void
test_grid_reference_fuzz (void)
{
	static const guint sizes[][2] = {
		{ 1, 1 }, { 1, 17 }, { 23, 1 }, { 2, 2 }, { 3, 7 }, { 16, 16 },
		{ 37, 21 }, { 96, 64 }, { 64, 3 }, { 5, 80 }
	};
	guint seed;

	for (seed = 0; seed < 100; seed++)
	{
		g_autoptr(LrgRandomStream) rng = lrg_random_stream_new (seed, 77);
		guint w = sizes[seed % G_N_ELEMENTS (sizes)][0];
		guint h = sizes[seed % G_N_ELEMENTS (sizes)][1];
		guint density = 20 + lrg_random_stream_bounded (rng, 70);
		g_autoptr(LrgProcgenGrid) grid = lrg_procgen_grid_new (w, h);
		g_autofree gint *dist = NULL;
		g_autofree gint *ref_dist = NULL;
		RefGrid *g;
		guint x;
		guint y;
		guint k;
		guint8 corridor = seed % 7 == 3 ? LRG_PROCGEN_CELL_WATER : LRG_PROCGEN_CELL_CORRIDOR;

		/* noise of floor, door, water (not walkable) and wall; a few
		 * grids make water walkable */
		for (y = 0; y < h; y++)
			for (x = 0; x < w; x++)
			{
				guint roll = lrg_random_stream_bounded (rng, 100);
				guint8 tag = LRG_PROCGEN_CELL_WALL;

				if (roll < density)
					tag = roll % 9 == 0 ? LRG_PROCGEN_CELL_DOOR : LRG_PROCGEN_CELL_FLOOR;
				else if (roll % 5 == 0)
					tag = LRG_PROCGEN_CELL_WATER;
				lrg_procgen_grid_set_cell (grid, (gint) x, (gint) y, tag);
			}
		if (seed % 11 == 5)
			lrg_procgen_grid_set_tag_walkable (grid, LRG_PROCGEN_CELL_WATER, TRUE);

		g = ref_grid_new (grid);
		g_assert_cmpuint (lrg_procgen_grid_label_regions (grid), ==, ref_label (g));
		assert_matches_ref (grid, g);

		/* distance maps from a few cells */
		ref_dist = g_new (gint, (gsize) w * h);
		for (k = 0; k < 4; k++)
		{
			gint fx = (gint) lrg_random_stream_bounded (rng, w);
			gint fy = (gint) lrg_random_stream_bounded (rng, h);

			dist = lrg_procgen_grid_distance_map (grid, fx, fy, NULL);
			ref_distances (g, fx, fy, ref_dist);
			g_assert_cmpmem (dist, (gsize) w * h * sizeof (gint), ref_dist, (gsize) w * h * sizeof (gint));
			g_clear_pointer (&dist, g_free);
		}

		/* corridors, then the labels they leave */
		g_assert_cmpuint (lrg_procgen_grid_connect_regions (grid, corridor), ==,
		                  ref_connect (g, corridor));
		ref_label (g);
		assert_matches_ref (grid, g);

		/* an edit after labelling is seen by the next labelling */
		lrg_procgen_grid_set_cell (grid, (gint) (w / 2), (gint) (h / 2), LRG_PROCGEN_CELL_WALL);
		g->cells[(h / 2) * w + w / 2] = LRG_PROCGEN_CELL_WALL;
		g_assert_cmpuint (lrg_procgen_grid_label_regions (grid), ==, ref_label (g));
		assert_matches_ref (grid, g);
		lrg_procgen_grid_set_tag_walkable (grid, LRG_PROCGEN_CELL_DOOR, FALSE);
		g->walkable[LRG_PROCGEN_CELL_DOOR] = FALSE;
		g_assert_cmpuint (lrg_procgen_grid_label_regions (grid), ==, ref_label (g));
		assert_matches_ref (grid, g);

		/* distance maps follow the edits too (they are cached per cell) */
		for (k = 0; k < 2; k++)
		{
			dist = lrg_procgen_grid_distance_map (grid, (gint) (w / 2), (gint) (h / 2) - (gint) k, NULL);
			ref_distances (g, (gint) (w / 2), (gint) (h / 2) - (gint) k, ref_dist);
			g_assert_cmpmem (dist, (gsize) w * h * sizeof (gint), ref_dist, (gsize) w * h * sizeof (gint));
			g_clear_pointer (&dist, g_free);
		}
		ref_grid_free (g);
	}
}

static void
test_grid_start_exit (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgProcgenGrid) grid = lrg_procgen_grid_new_from_string (
		"##########\n"
		"#..#.....#\n"
		"##.#.###.#\n"
		"#....#...#\n"
		"##########\n", &error);
	g_autofree gint *dist = NULL;
	gsize n;
	gsize i;
	gint sx;
	gint sy;
	gint ex;
	gint ey;
	gint best = 0;
	gint d;

	g_assert_no_error (error);
	g_assert_false (lrg_procgen_grid_get_start (grid, NULL, NULL));
	g_assert_true (lrg_procgen_grid_place_start_exit (grid));
	g_assert_true (lrg_procgen_grid_get_start (grid, &sx, &sy));
	g_assert_true (lrg_procgen_grid_get_exit (grid, &ex, &ey));
	g_assert_true (lrg_procgen_grid_is_walkable (grid, sx, sy));
	g_assert_true (lrg_procgen_grid_is_walkable (grid, ex, ey));

	/* on this tree-shaped map the double sweep is the exact diameter */
	d = lrg_procgen_grid_path_distance (grid, sx, sy, ex, ey);
	for (i = 0; i < 40; i++)
	{
		gint x = (gint) (i % 10);
		gint y = (gint) (i / 10);

		if (!lrg_procgen_grid_is_walkable (grid, x, y))
			continue;
		dist = lrg_procgen_grid_distance_map (grid, x, y, &n);
		g_assert_cmpuint (n, ==, 50);
		for (n = 0; n < 50; n++)
			best = MAX (best, dist[n]);
		g_clear_pointer (&dist, g_free);
	}
	g_assert_cmpint (d, ==, best);
}

/* ==========================================================================
 * BSP
 * ========================================================================== */

static void
test_bsp_determinism (void)
{
	g_autoptr(LrgProcgenBsp) bsp = lrg_procgen_bsp_new (64, 48);
	g_autoptr(LrgProcgenGrid) a = NULL;
	g_autoptr(LrgProcgenGrid) b = NULL;
	g_autoptr(LrgProcgenGrid) c = NULL;
	g_autoptr(GError) error = NULL;

	a = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (bsp), 42, &error);
	g_assert_no_error (error);
	b = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (bsp), 42, &error);
	g_assert_no_error (error);
	c = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (bsp), 43, &error);
	g_assert_no_error (error);

	g_assert_cmpuint (lrg_procgen_grid_hash (a), ==, lrg_procgen_grid_hash (b));
	g_assert_cmpuint (lrg_procgen_grid_hash (a), !=, lrg_procgen_grid_hash (c));
	g_assert_cmpuint (lrg_procgen_grid_get_room_count (a), ==, lrg_procgen_grid_get_room_count (b));
	check_golden ("bsp seed 42", lrg_procgen_grid_hash (a), GOLDEN_BSP);
}

static void
test_bsp_invariants (void)
{
	g_autoptr(LrgProcgenBsp) bsp = lrg_procgen_bsp_new (60, 40);
	guint64 seed;

	for (seed = 1; seed <= 25; seed++)
	{
		g_autoptr(LrgProcgenGrid) grid = NULL;
		g_autoptr(GError) error = NULL;
		guint n;
		guint i;
		guint j;

		grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (bsp), seed, &error);
		g_assert_no_error (error);
		g_assert_true (lrg_procgen_grid_is_connected (grid));
		assert_border_walls (grid);

		/* rooms: inside the interior, on floor, sized by the parameters, disjoint with a wall gap */
		n = lrg_procgen_grid_get_room_count (grid);
		g_assert_cmpuint (n, >=, 2);
		for (i = 0; i < n; i++)
		{
			const LrgProcgenRoom *r = lrg_procgen_grid_get_room (grid, i);
			gint x;
			gint y;

			g_assert_cmpint (r->x, >=, 1);
			g_assert_cmpint (r->y, >=, 1);
			g_assert_cmpint (r->x + r->width, <=, 59);
			g_assert_cmpint (r->y + r->height, <=, 39);
			g_assert_cmpint (r->width, >=, 3);
			g_assert_cmpint (r->width, <=, 12);
			g_assert_cmpint (r->height, >=, 3);
			g_assert_cmpint (r->height, <=, 12);
			for (y = r->y; y < r->y + r->height; y++)
				for (x = r->x; x < r->x + r->width; x++)
					g_assert_cmpuint (lrg_procgen_grid_get_cell (grid, x, y), ==, LRG_PROCGEN_CELL_FLOOR);
			for (j = i + 1; j < n; j++)
			{
				const LrgProcgenRoom *s = lrg_procgen_grid_get_room (grid, j);

				g_assert_true (r->x + r->width < s->x || s->x + s->width < r->x ||
				               r->y + r->height < s->y || s->y + s->height < r->y);
			}
		}

		/* every door touches a room and is flanked by walls */
		for (i = 0; i < 60 * 40; i++)
		{
			gint x = (gint) (i % 60);
			gint y = (gint) (i / 60);

			if (lrg_procgen_grid_get_cell (grid, x, y) != LRG_PROCGEN_CELL_DOOR)
				continue;
			g_assert_true (lrg_procgen_grid_room_at (grid, x - 1, y) >= 0 ||
			               lrg_procgen_grid_room_at (grid, x + 1, y) >= 0 ||
			               lrg_procgen_grid_room_at (grid, x, y - 1) >= 0 ||
			               lrg_procgen_grid_room_at (grid, x, y + 1) >= 0);
		}
	}
}

static void
test_bsp_bad_params (void)
{
	g_autoptr(LrgProcgenBsp) bsp = lrg_procgen_bsp_new (6, 6);
	g_autoptr(LrgProcgenGrid) grid = NULL;
	g_autoptr(GError) error = NULL;

	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (bsp), 1, &error);
	g_assert_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_TOO_SMALL);
	g_assert_null (grid);
	g_clear_error (&error);

	lrg_procgen_generator_set_width (LRG_PROCGEN_GENERATOR (bsp), 40);
	lrg_procgen_generator_set_height (LRG_PROCGEN_GENERATOR (bsp), 40);
	lrg_procgen_bsp_set_min_room_size (bsp, 10);
	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (bsp), 1, &error);
	g_assert_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG);
	g_assert_null (grid);
}

/* ==========================================================================
 * Caves, drunkard, noise
 * ========================================================================== */

static void
test_caves (void)
{
	g_autoptr(LrgProcgenCaves) caves = lrg_procgen_caves_new (64, 48);
	guint64 seed;

	for (seed = 1; seed <= 15; seed++)
	{
		g_autoptr(LrgProcgenGrid) grid = NULL;
		g_autoptr(LrgProcgenGrid) again = NULL;
		g_autoptr(GError) error = NULL;

		grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (caves), seed, &error);
		g_assert_no_error (error);
		again = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (caves), seed, &error);
		g_assert_no_error (error);
		g_assert_cmpuint (lrg_procgen_grid_hash (grid), ==, lrg_procgen_grid_hash (again));
		g_assert_true (lrg_procgen_grid_is_connected (grid));
		g_assert_cmpuint (lrg_procgen_grid_get_region_count (grid), ==, 1);
		g_assert_cmpuint (lrg_procgen_grid_get_region_size (grid, 0), >=, 16);
		assert_border_walls (grid);
		if (seed == 7)
			check_golden ("caves seed 7", lrg_procgen_grid_hash (grid), GOLDEN_CAVES);
	}

	/* keep-largest instead of connecting */
	{
		g_autoptr(LrgProcgenGrid) grid = NULL;
		g_autoptr(GError) error = NULL;

		lrg_procgen_caves_set_keep_largest (caves, TRUE);
		lrg_procgen_caves_set_min_region_size (caves, 0);
		grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (caves), 3, &error);
		g_assert_no_error (error);
		g_assert_true (lrg_procgen_grid_is_connected (grid));
		g_assert_cmpuint (lrg_procgen_grid_count_cells (grid, LRG_PROCGEN_CELL_CORRIDOR), ==, 0);
	}

	/* a solid fill leaves nothing */
	{
		g_autoptr(LrgProcgenGrid) grid = NULL;
		g_autoptr(GError) error = NULL;

		lrg_procgen_caves_set_fill_percent (caves, 100);
		grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (caves), 3, &error);
		g_assert_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_FAILED);
		g_assert_null (grid);
	}
}

static void
test_drunkard (void)
{
	g_autoptr(LrgProcgenDrunkard) walk = lrg_procgen_drunkard_new (50, 30);
	g_autoptr(LrgProcgenGrid) grid = NULL;
	g_autoptr(LrgProcgenGrid) multi = NULL;
	g_autoptr(GError) error = NULL;

	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (walk), 99, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_procgen_grid_count_walkable (grid), ==, 48 * 28 * 40 / 100);
	g_assert_true (lrg_procgen_grid_is_connected (grid));
	assert_border_walls (grid);
	check_golden ("drunkard seed 99", lrg_procgen_grid_hash (grid), GOLDEN_DRUNKARD);

	/* restarting walkers stay connected: they start on open floor */
	lrg_procgen_drunkard_set_walk_length (walk, 50);
	lrg_procgen_drunkard_set_coverage_percent (walk, 55);
	multi = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (walk), 99, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_procgen_grid_is_connected (multi));
	g_assert_cmpuint (lrg_procgen_grid_count_walkable (multi), ==, 48 * 28 * 55 / 100);
}

static void
test_noise (void)
{
	g_autoptr(LrgProcgenNoise) noise = lrg_procgen_noise_new (48, 32);
	g_autoptr(LrgProcgenGrid) grid = NULL;
	g_autoptr(GError) error = NULL;
	gint x;
	gint y;

	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (noise), 5, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_procgen_grid_has_heights (grid));
	assert_border_walls (grid);
	for (y = 1; y < 31; y++)
		for (x = 1; x < 47; x++)
		{
			gfloat h = lrg_procgen_grid_get_height_at (grid, x, y);
			guint8 tag = lrg_procgen_grid_get_cell (grid, x, y);

			g_assert_cmpfloat (h, >=, 0.0f);
			g_assert_cmpfloat (h, <=, 1.0f);
			if (tag == LRG_PROCGEN_CELL_WATER)
				g_assert_cmpfloat (h, <, 0.36f);
			else if (tag == LRG_PROCGEN_CELL_WALL)
				g_assert_cmpfloat (h, >=, 0.69f);
			else
				g_assert_cmpuint (tag, ==, LRG_PROCGEN_CELL_FLOOR);
		}
	g_assert_cmpuint (lrg_procgen_grid_count_cells (grid, LRG_PROCGEN_CELL_WATER), >, 0);
	g_assert_cmpuint (lrg_procgen_grid_count_cells (grid, LRG_PROCGEN_CELL_FLOOR), >, 0);
	check_golden ("noise seed 5", lrg_procgen_grid_hash (grid), GOLDEN_NOISE);

	lrg_procgen_noise_set_water_percent (noise, 80);
	g_clear_object (&grid);
	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (noise), 5, &error);
	g_assert_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG);
}

/* ==========================================================================
 * Wave function collapse
 * ========================================================================== */

static void
test_wfc_learned (void)
{
	g_autoptr(LrgProcgenGrid) sample = NULL;
	g_autoptr(LrgProcgenWfc) wfc = lrg_procgen_wfc_new (32, 20);
	g_autoptr(GError) error = NULL;
	guint64 seed;

	sample = lrg_procgen_grid_new_from_string (wfc_sample, &error);
	g_assert_no_error (error);
	lrg_procgen_wfc_learn (wfc, sample);

	/* tiles: one per tag, ascending, named by nick, weighted by count */
	g_assert_cmpuint (lrg_procgen_wfc_get_tile_count (wfc), ==, 4);
	g_assert_cmpstr (lrg_procgen_wfc_get_tile_name (wfc, 0), ==, "wall");
	g_assert_cmpstr (lrg_procgen_wfc_get_tile_name (wfc, 3), ==, "door");
	g_assert_cmpfloat (lrg_procgen_wfc_get_tile_weight (wfc, 3), ==, 2.0);
	g_assert_true (lrg_procgen_wfc_check (wfc, sample));
	g_assert_false (lrg_procgen_wfc_is_allowed (wfc, 3, 3, LRG_PROCGEN_DIRECTION_RIGHT));
	lrg_procgen_wfc_set_border_tile (wfc, "wall");

	for (seed = 1; seed <= 10; seed++)
	{
		g_autoptr(LrgProcgenGrid) grid = NULL;
		g_autoptr(LrgProcgenGrid) again = NULL;

		grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (wfc), seed, &error);
		g_assert_no_error (error);
		again = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (wfc), seed, &error);
		g_assert_no_error (error);
		g_assert_cmpuint (lrg_procgen_grid_hash (grid), ==, lrg_procgen_grid_hash (again));
		/* the output satisfies every adjacency rule */
		g_assert_true (lrg_procgen_wfc_check (wfc, grid));
		assert_border_walls (grid);
		if (seed == 1)
			check_golden ("wfc seed 1", lrg_procgen_grid_hash (grid), GOLDEN_WFC);
	}
}

/* a two-tile checkerboard is the only solution of "never next to yourself" */
static void
test_wfc_checkerboard (void)
{
	g_autoptr(LrgProcgenWfc) wfc = lrg_procgen_wfc_new (9, 7);
	g_autoptr(LrgProcgenGrid) grid = NULL;
	g_autoptr(GError) error = NULL;
	gint a;
	gint b;
	gint x;
	gint y;
	guint d;

	a = lrg_procgen_wfc_add_tile (wfc, "black", LRG_PROCGEN_CELL_WALL, 1.0);
	b = lrg_procgen_wfc_add_tile (wfc, "white", LRG_PROCGEN_CELL_FLOOR, 1.0);
	g_assert_cmpint (a, ==, 0);
	g_assert_cmpint (b, ==, 1);
	g_assert_cmpint (lrg_procgen_wfc_add_tile (wfc, "black", LRG_PROCGEN_CELL_DOOR, 1.0), ==, -1);
	for (d = 0; d < 4; d++)
		lrg_procgen_wfc_allow (wfc, (guint) a, (guint) b, (LrgProcgenDirection) d);
	g_assert_true (lrg_procgen_wfc_is_allowed (wfc, (guint) b, (guint) a, LRG_PROCGEN_DIRECTION_UP));

	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (wfc), 11, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_procgen_wfc_check (wfc, grid));
	for (y = 0; y < 7; y++)
		for (x = 0; x < 9; x++)
			g_assert_cmpuint (lrg_procgen_grid_get_cell (grid, x, y), ==,
			                  ((x + y) % 2 == 0) ? lrg_procgen_grid_get_cell (grid, 0, 0)
			                                     : lrg_procgen_grid_get_cell (grid, 1, 0));
	g_assert_cmpuint (lrg_procgen_grid_get_cell (grid, 0, 0), !=, lrg_procgen_grid_get_cell (grid, 1, 0));
	g_assert_cmpuint (lrg_procgen_wfc_get_last_attempts (wfc), ==, 1);
}

/* three colours, never two equal neighbours: greedy choices dead-end, backtracking recovers */
static void
test_wfc_backtracking (void)
{
	g_autoptr(LrgProcgenWfc) wfc = lrg_procgen_wfc_new (24, 24);
	guint backtracks = 0;
	guint64 seed;
	guint i;
	guint j;
	guint d;

	lrg_procgen_wfc_add_tile (wfc, "red", LRG_PROCGEN_CELL_CUSTOM, 1.0);
	lrg_procgen_wfc_add_tile (wfc, "green", LRG_PROCGEN_CELL_CUSTOM + 1, 1.0);
	lrg_procgen_wfc_add_tile (wfc, "blue", LRG_PROCGEN_CELL_CUSTOM + 2, 1.0);
	for (i = 0; i < 3; i++)
		for (j = 0; j < 3; j++)
			if (i != j)
				for (d = 0; d < 4; d++)
					lrg_procgen_wfc_allow (wfc, i, j, (LrgProcgenDirection) d);
	lrg_procgen_wfc_set_max_attempts (wfc, 50);

	for (seed = 1; seed <= 10; seed++)
	{
		g_autoptr(LrgProcgenGrid) grid = NULL;
		g_autoptr(GError) error = NULL;

		grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (wfc), seed, &error);
		g_assert_no_error (error);
		g_assert_true (lrg_procgen_wfc_check (wfc, grid));
		backtracks += lrg_procgen_wfc_get_last_backtracks (wfc);
	}
	g_test_message ("3-colouring backtracks over 10 seeds: %u", backtracks);
}

static void
test_wfc_contradiction (void)
{
	g_autoptr(LrgProcgenWfc) wfc = lrg_procgen_wfc_new (6, 6);
	g_autoptr(LrgProcgenWfc) empty = lrg_procgen_wfc_new (6, 6);
	g_autoptr(LrgProcgenGrid) grid = NULL;
	g_autoptr(GError) error = NULL;
	gint a;

	/* one tile that may only sit above or below itself: no row of 2 exists */
	a = lrg_procgen_wfc_add_tile (wfc, "pillar", LRG_PROCGEN_CELL_WALL, 1.0);
	lrg_procgen_wfc_allow (wfc, (guint) a, (guint) a, LRG_PROCGEN_DIRECTION_DOWN);
	lrg_procgen_wfc_set_max_attempts (wfc, 4);
	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (wfc), 1, &error);
	g_assert_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_CONTRADICTION);
	g_assert_null (grid);
	g_assert_cmpuint (lrg_procgen_wfc_get_last_attempts (wfc), ==, 4);
	g_clear_error (&error);

	/* a 1-wide map is fine with the same model */
	lrg_procgen_generator_set_width (LRG_PROCGEN_GENERATOR (wfc), 1);
	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (wfc), 1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_procgen_grid_count_cells (grid, LRG_PROCGEN_CELL_WALL), ==, 6);
	g_clear_object (&grid);

	/* no tiles, unknown border tile */
	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (empty), 1, &error);
	g_assert_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG);
	g_clear_error (&error);
	lrg_procgen_wfc_set_border_tile (wfc, "nope");
	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (wfc), 1, &error);
	g_assert_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG);
}

/* ==========================================================================
 * Tile rules, tilemap layer, navigation grid
 * ========================================================================== */

static void
test_tile_rules (void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(LrgProcgenGrid) grid = lrg_procgen_grid_new_from_string (
		"#####\n"
		"#...#\n"
		"#.+.#\n"
		"#...#\n"
		"#####\n", &error);
	g_autoptr(LrgProcgenTileRules) rules = lrg_procgen_tile_rules_new ();
	g_autoptr(LrgTilemapLayer) layer = NULL;
	GHashTable *seen;
	guint m;

	g_assert_no_error (error);

	/* the 47 blob variants are distinct and cover 0..46 */
	seen = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (m = 0; m < 256; m++)
		g_hash_table_add (seen, GUINT_TO_POINTER (lrg_procgen_tile_rules_blob_index (m)));
	g_assert_cmpuint (g_hash_table_size (seen), ==, LRG_PROCGEN_BLOB_VARIANTS);
	g_assert_cmpuint (lrg_procgen_tile_rules_blob_index (0), ==, 0);
	g_assert_cmpuint (lrg_procgen_tile_rules_blob_index (255), ==, 46);
	g_hash_table_unref (seen);

	lrg_procgen_tile_rules_set_default_tile (rules, 99);
	lrg_procgen_tile_rules_set_rule (rules, LRG_PROCGEN_CELL_FLOOR, 1, LRG_PROCGEN_AUTOTILE_NONE);
	lrg_procgen_tile_rules_set_rule (rules, LRG_PROCGEN_CELL_WALL, 100, LRG_PROCGEN_AUTOTILE_CARDINAL);

	/* cardinal masks of walls: N=1 E=2 S=4 W=8, outside matches */
	g_assert_cmpuint (lrg_procgen_tile_rules_neighbour_mask (rules, grid, 0, 0, LRG_PROCGEN_AUTOTILE_CARDINAL), ==, 15);
	g_assert_cmpuint (lrg_procgen_tile_rules_neighbour_mask (rules, grid, 2, 0, LRG_PROCGEN_AUTOTILE_CARDINAL), ==, 11);
	g_assert_cmpuint (lrg_procgen_tile_rules_neighbour_mask (rules, grid, 0, 2, LRG_PROCGEN_AUTOTILE_CARDINAL), ==, 13);
	g_assert_cmpuint (lrg_procgen_tile_rules_resolve (rules, grid, 2, 0), ==, 111);
	g_assert_cmpuint (lrg_procgen_tile_rules_resolve (rules, grid, 1, 1), ==, 1);
	g_assert_cmpuint (lrg_procgen_tile_rules_resolve (rules, grid, 2, 2), ==, 99); /* door: default */
	lrg_procgen_tile_rules_set_edge_connects (rules, FALSE);
	g_assert_cmpuint (lrg_procgen_tile_rules_neighbour_mask (rules, grid, 0, 0, LRG_PROCGEN_AUTOTILE_CARDINAL), ==, 6);

	/* blob: the floor around the door, with and without doors connecting */
	lrg_procgen_tile_rules_set_rule (rules, LRG_PROCGEN_CELL_FLOOR, 200, LRG_PROCGEN_AUTOTILE_BLOB);
	g_assert_cmpuint (lrg_procgen_tile_rules_neighbour_mask (rules, grid, 1, 1, LRG_PROCGEN_AUTOTILE_BLOB), ==, 4 | 16);
	lrg_procgen_tile_rules_set_connects (rules, LRG_PROCGEN_CELL_FLOOR, LRG_PROCGEN_CELL_DOOR, TRUE);
	g_assert_cmpuint (lrg_procgen_tile_rules_neighbour_mask (rules, grid, 1, 1, LRG_PROCGEN_AUTOTILE_BLOB), ==, 4 | 8 | 16);
	g_assert_cmpuint (lrg_procgen_tile_rules_resolve (rules, grid, 1, 1), ==,
	                  200 + lrg_procgen_tile_rules_blob_index (4 | 8 | 16));

	/* an explicit variant wins */
	lrg_procgen_tile_rules_set_variant (rules, LRG_PROCGEN_CELL_FLOOR, 4 | 8 | 16, 777);
	g_assert_cmpuint (lrg_procgen_tile_rules_resolve (rules, grid, 1, 1), ==, 777);

	/* the layer */
	layer = lrg_procgen_tile_rules_build_layer (rules, grid);
	g_assert_cmpuint (lrg_tilemap_layer_get_width (layer), ==, 5);
	g_assert_cmpuint (lrg_tilemap_layer_get_height (layer), ==, 5);
	g_assert_cmpstr (lrg_tilemap_layer_get_name (layer), ==, "procgen");
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 1, 1), ==, 777);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 2, 2), ==, 99);
	g_assert_cmpuint (lrg_tilemap_layer_get_tile (layer, 0, 0), ==, 106);

	lrg_procgen_tile_rules_remove_rule (rules, LRG_PROCGEN_CELL_FLOOR);
	g_assert_false (lrg_procgen_tile_rules_get_rule (rules, LRG_PROCGEN_CELL_FLOOR, NULL, NULL));
	g_assert_false (lrg_procgen_tile_rules_get_variant (rules, LRG_PROCGEN_CELL_FLOOR, 4 | 8 | 16, NULL));
}

static void
test_nav_grid (void)
{
	g_autoptr(LrgProcgenBsp) bsp = lrg_procgen_bsp_new (48, 32);
	g_autoptr(LrgProcgenGrid) grid = NULL;
	g_autoptr(LrgNavGrid) nav = NULL;
	g_autoptr(LrgPathfinder) pathfinder = NULL;
	g_autoptr(LrgPath) path = NULL;
	g_autoptr(GError) error = NULL;
	gint sx;
	gint sy;
	gint ex;
	gint ey;
	gint x;
	gint y;

	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (bsp), 8, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_procgen_grid_place_start_exit (grid));
	lrg_procgen_grid_get_start (grid, &sx, &sy);
	lrg_procgen_grid_get_exit (grid, &ex, &ey);

	nav = lrg_procgen_grid_to_nav_grid (grid);
	g_assert_cmpuint (lrg_nav_grid_get_width (nav), ==, 48);
	g_assert_cmpuint (lrg_nav_grid_get_height (nav), ==, 32);
	for (y = 0; y < 32; y++)
		for (x = 0; x < 48; x++)
			g_assert_cmpint (lrg_nav_grid_is_walkable (nav, x, y), ==,
			                 lrg_procgen_grid_is_walkable (grid, x, y));

	/* A* agrees with the grid's own BFS distance */
	lrg_nav_grid_set_allow_diagonal (nav, FALSE);
	pathfinder = lrg_pathfinder_new (nav);
	path = lrg_pathfinder_find_path (pathfinder, sx, sy, ex, ey, &error);
	g_assert_no_error (error);
	g_assert_nonnull (path);
	g_assert_cmpint ((gint) lrg_path_get_length (path) - 1, ==,
	                 lrg_procgen_grid_path_distance (grid, sx, sy, ex, ey));
}

/* ==========================================================================
 * Recipes (YAML) and spawning
 * ========================================================================== */

static const gchar *bsp_yaml =
	"generator: bsp\n"
	"width: 56\n"
	"height: 40\n"
	"seed: 1234\n"
	"params:\n"
	"  min-room-size: 4\n"
	"  max-room-size: 9\n"
	"  doors: true\n"
	"post:\n"
	"  connect: true\n"
	"  start-exit: true\n"
	"spawns:\n"
	"  - { kind: goblin, count: 6, min-distance: 10 }\n"
	"  - { kind: chest, count: 3, rooms-only: true }\n"
	"tiles:\n"
	"  default: 0\n"
	"  rules:\n"
	"    - { tag: wall, tile: 17, autotile: blob, connects: [door] }\n"
	"    - { tag: floor, tile: 1 }\n"
	"    - { tag: corridor, tile: 2 }\n"
	"    - { tag: door, tile: 3, variants: [ { mask: 0, tile: 4 } ] }\n";

static void
test_dungeon_yaml_round_trip (void)
{
	g_autoptr(LrgProcgenDungeon) dungeon = NULL;
	g_autoptr(LrgProcgenDungeon) reloaded = NULL;
	g_autoptr(LrgProcgenGrid) a = NULL;
	g_autoptr(LrgProcgenGrid) b = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *first = NULL;
	g_autofree gchar *second = NULL;
	g_autofree gchar *dir = NULL;
	g_autofree gchar *path = NULL;
	g_autoptr(LrgProcgenDungeon) from_file = NULL;
	LrgProcgenGenerator *gen;
	const gchar *kind;
	guint count;
	guint min_distance;
	gboolean rooms_only;
	guint i;

	dungeon = lrg_procgen_dungeon_new_from_data (bsp_yaml, -1, &error);
	g_assert_no_error (error);
	gen = lrg_procgen_dungeon_get_generator (dungeon);
	g_assert_true (LRG_IS_PROCGEN_BSP (gen));
	g_assert_cmpuint (lrg_procgen_generator_get_width (gen), ==, 56);
	g_assert_cmpuint (lrg_procgen_bsp_get_min_room_size (LRG_PROCGEN_BSP (gen)), ==, 4);
	g_assert_cmpuint (lrg_procgen_bsp_get_max_room_size (LRG_PROCGEN_BSP (gen)), ==, 9);
	g_assert_cmpuint (lrg_procgen_dungeon_get_seed (dungeon), ==, 1234);
	g_assert_cmpuint (lrg_procgen_dungeon_get_spawn_rule_count (dungeon), ==, 2);
	g_assert_true (lrg_procgen_dungeon_get_spawn_rule (dungeon, 1, &kind, &count, &min_distance, &rooms_only));
	g_assert_cmpstr (kind, ==, "chest");
	g_assert_cmpuint (count, ==, 3);
	g_assert_true (rooms_only);

	/* save, load, save: the same document and the same maps */
	first = lrg_procgen_dungeon_to_data (dungeon, &error);
	g_assert_no_error (error);
	reloaded = lrg_procgen_dungeon_new_from_data (first, -1, &error);
	g_assert_no_error (error);
	second = lrg_procgen_dungeon_to_data (reloaded, &error);
	g_assert_no_error (error);
	g_assert_cmpstr (first, ==, second);

	a = lrg_procgen_dungeon_generate (dungeon, lrg_procgen_dungeon_get_seed (dungeon), &error);
	g_assert_no_error (error);
	b = lrg_procgen_dungeon_generate (reloaded, 1234, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_procgen_grid_hash (a), ==, lrg_procgen_grid_hash (b));
	g_assert_cmpuint (lrg_procgen_grid_get_spawn_count (a), ==, lrg_procgen_grid_get_spawn_count (b));
	for (i = 0; i < lrg_procgen_grid_get_spawn_count (a); i++)
	{
		const LrgProcgenSpawn *sa = lrg_procgen_grid_get_spawn (a, i);
		const LrgProcgenSpawn *sb = lrg_procgen_grid_get_spawn (b, i);

		g_assert_cmpstr (sa->kind, ==, sb->kind);
		g_assert_cmpint (sa->x, ==, sb->x);
		g_assert_cmpint (sa->y, ==, sb->y);
	}
	check_golden ("dungeon recipe seed 1234", lrg_procgen_grid_hash (a), GOLDEN_DUNGEON);

	/* the same through a file */
	dir = g_dir_make_tmp ("test-procgen-XXXXXX", &error);
	g_assert_no_error (error);
	path = g_build_filename (dir, "dungeon.yaml", NULL);
	g_assert_true (lrg_procgen_dungeon_save_to_file (dungeon, path, &error));
	g_assert_no_error (error);
	from_file = lrg_procgen_dungeon_new_from_file (path, &error);
	g_assert_no_error (error);
	g_clear_pointer (&second, g_free);
	second = lrg_procgen_dungeon_to_data (from_file, &error);
	g_assert_cmpstr (first, ==, second);
	g_assert_cmpint (g_remove (path), ==, 0);
	g_assert_cmpint (g_rmdir (dir), ==, 0);
}

static void
test_dungeon_yaml_wfc (void)
{
	g_autoptr(GString) yaml = g_string_new ("generator: wfc\nwidth: 30\nheight: 18\n"
	                                        "params: { border-tile: wall, max-attempts: 20 }\n"
	                                        "post: { connect: true, start-exit: false }\n"
	                                        "wfc:\n  sample: |\n");
	g_auto(GStrv) lines = g_strsplit (wfc_sample, "\n", -1);
	g_autoptr(LrgProcgenDungeon) dungeon = NULL;
	g_autoptr(LrgProcgenDungeon) reloaded = NULL;
	g_autoptr(LrgProcgenGrid) a = NULL;
	g_autoptr(LrgProcgenGrid) b = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *first = NULL;
	g_autofree gchar *second = NULL;
	guint i;

	for (i = 0; lines[i] != NULL; i++)
		if (lines[i][0] != '\0')
			g_string_append_printf (yaml, "    %s\n", lines[i]);

	dungeon = lrg_procgen_dungeon_new_from_data (yaml->str, -1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_procgen_wfc_get_tile_count (LRG_PROCGEN_WFC (lrg_procgen_dungeon_get_generator (dungeon))), ==, 4);

	/* the learned model is written as explicit tiles and rules */
	first = lrg_procgen_dungeon_to_data (dungeon, &error);
	g_assert_no_error (error);
	g_assert_nonnull (strstr (first, "rules"));
	reloaded = lrg_procgen_dungeon_new_from_data (first, -1, &error);
	g_assert_no_error (error);
	second = lrg_procgen_dungeon_to_data (reloaded, &error);
	g_assert_cmpstr (first, ==, second);

	a = lrg_procgen_dungeon_generate (dungeon, 77, &error);
	g_assert_no_error (error);
	b = lrg_procgen_dungeon_generate (reloaded, 77, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_procgen_grid_hash (a), ==, lrg_procgen_grid_hash (b));
	g_assert_true (lrg_procgen_grid_is_connected (a));
}

/* the recipe shipped with the examples loads and makes a connected map */
static void
test_dungeon_example_file (void)
{
	g_autofree gchar *path = g_test_build_filename (G_TEST_DIST, "..", "examples", "data", "dungeon.yaml", NULL);
	g_autoptr(LrgProcgenDungeon) dungeon = NULL;
	g_autoptr(LrgProcgenGrid) grid = NULL;
	g_autoptr(LrgTilemapLayer) layer = NULL;
	g_autoptr(GError) error = NULL;

	dungeon = lrg_procgen_dungeon_new_from_file (path, &error);
	g_assert_no_error (error);
	grid = lrg_procgen_dungeon_generate (dungeon, lrg_procgen_dungeon_get_seed (dungeon), &error);
	g_assert_no_error (error);
	g_assert_true (lrg_procgen_grid_is_connected (grid));
	g_assert_cmpuint (lrg_procgen_grid_get_spawn_count (grid), ==, 14);
	layer = lrg_procgen_dungeon_build_layer (dungeon, grid);
	g_assert_cmpuint (lrg_tilemap_layer_get_width (layer), ==, 64);

	/* a missing file is a file error */
	g_clear_object (&dungeon);
	dungeon = lrg_procgen_dungeon_new_from_file ("/nonexistent/dungeon.yaml", &error);
	g_assert_null (dungeon);
	g_assert_error (error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
}

static void
test_dungeon_yaml_errors (void)
{
	static const gchar *bad[] = {
		"generator: bsp\nwidht: 10\n",                          /* unknown key */
		"generator: mazes\n",                                   /* unknown generator */
		"width: 10\n",                                          /* no generator */
		"generator: bsp\nparams: { room-size: 4 }\n",           /* unknown parameter */
		"generator: caves\nparams: { fill-percent: 140 }\n",    /* out of range */
		"generator: caves\nparams: { connect: maybe }\n",       /* not a boolean */
		"generator: bsp\nwidth: 0\n",                           /* zero size */
		"generator: bsp\nwfc: { sample: \"#.\" }\n",            /* wfc section on bsp */
		"generator: wfc\nwfc: { rules: [ { a: x, b: y } ] }\n", /* unknown tiles */
		"generator: bsp\ntiles: { rules: [ { tag: lava, tile: 3 } ] }\n", /* unknown tag */
		"generator: bsp\nversion: 9\n",                         /* future version */
		"- not\n- a mapping\n",
		"generator: [\n",                                       /* invalid YAML */
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS (bad); i++)
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(LrgProcgenDungeon) dungeon = lrg_procgen_dungeon_new_from_data (bad[i], -1, &error);

		g_test_message ("bad document %u: %s", i, error != NULL ? error->message : "(accepted)");
		g_assert_null (dungeon);
		g_assert_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG);
	}
}

/* bare generator defs through the generic data loader */
static void
test_data_loader (void)
{
	g_autoptr(LrgRegistry) registry = lrg_registry_new ();
	g_autoptr(LrgDataLoader) loader = lrg_data_loader_new ();
	g_autoptr(GObject) object = NULL;
	g_autoptr(LrgProcgenGrid) grid = NULL;
	g_autoptr(GError) error = NULL;

	lrg_procgen_register_types (registry);
	g_assert_true (lrg_registry_is_registered (registry, "procgen-caves"));
	g_assert_true (lrg_registry_is_registered (registry, "procgen-dungeon"));
	lrg_data_loader_set_registry (loader, registry);

	object = lrg_data_loader_load_data (loader,
	                                    "type: procgen-caves\n"
	                                    "width: 40\n"
	                                    "height: 30\n"
	                                    "fill-percent: 42\n"
	                                    "keep-largest: true\n", -1, &error);
	g_assert_no_error (error);
	g_assert_true (LRG_IS_PROCGEN_CAVES (object));
	g_assert_cmpuint (lrg_procgen_caves_get_fill_percent (LRG_PROCGEN_CAVES (object)), ==, 42);
	g_assert_true (lrg_procgen_caves_get_keep_largest (LRG_PROCGEN_CAVES (object)));
	grid = lrg_procgen_generator_generate (LRG_PROCGEN_GENERATOR (object), 1, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_procgen_grid_get_width (grid), ==, 40);
	g_assert_true (lrg_procgen_grid_is_connected (grid));
}

typedef struct
{
	guint calls;
} SpawnCount;

static LrgGameObject *
spawn_cb (LrgProcgenGrid        *grid,
          const LrgProcgenSpawn *spawn,
          gfloat                 world_x,
          gfloat                 world_y,
          gpointer               user_data)
{
	SpawnCount *count = user_data;
	LrgGameObject *object;

	count->calls++;
	g_assert_cmpfloat (world_x, ==, ((gfloat) spawn->x + 0.5f) * 16.0f);
	g_assert_cmpfloat (world_y, ==, ((gfloat) spawn->y + 0.5f) * 16.0f);
	if (g_strcmp0 (spawn->kind, "chest") == 0)
		return NULL; /* chests are not entities in this game */
	object = lrg_game_object_new_at (world_x, world_y);
	grl_entity_set_tag (GRL_ENTITY (object), "monster");
	return object;
}

static void
test_dungeon_spawns (void)
{
	g_autoptr(LrgProcgenDungeon) dungeon = NULL;
	g_autoptr(LrgProcgenGrid) grid = NULL;
	g_autoptr(LrgWorld) world = lrg_world_new ();
	g_autoptr(LrgWorld) plain = lrg_world_new ();
	g_autoptr(LrgRegistry) registry = lrg_registry_new ();
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) cells = g_hash_table_new (g_direct_hash, g_direct_equal);
	SpawnCount count = { 0 };
	guint goblins = 0;
	guint chests = 0;
	gint sx;
	gint sy;
	gint ex;
	gint ey;
	guint i;

	dungeon = lrg_procgen_dungeon_new_from_data (bsp_yaml, -1, &error);
	g_assert_no_error (error);
	grid = lrg_procgen_dungeon_generate (dungeon, 5, &error);
	g_assert_no_error (error);
	g_assert_true (lrg_procgen_grid_get_start (grid, &sx, &sy));
	g_assert_true (lrg_procgen_grid_get_exit (grid, &ex, &ey));
	g_assert_true (lrg_procgen_grid_is_connected (grid));

	/* every point: walkable, distinct, not start/exit, rules respected */
	g_hash_table_add (cells, GINT_TO_POINTER (sy * 56 + sx));
	g_hash_table_add (cells, GINT_TO_POINTER (ey * 56 + ex));
	for (i = 0; i < lrg_procgen_grid_get_spawn_count (grid); i++)
	{
		const LrgProcgenSpawn *s = lrg_procgen_grid_get_spawn (grid, i);

		g_assert_true (lrg_procgen_grid_is_walkable (grid, s->x, s->y));
		g_assert_true (g_hash_table_add (cells, GINT_TO_POINTER (s->y * 56 + s->x)));
		if (g_strcmp0 (s->kind, "goblin") == 0)
		{
			goblins++;
			g_assert_cmpint (lrg_procgen_grid_path_distance (grid, sx, sy, s->x, s->y), >=, 10);
		}
		else
		{
			chests++;
			g_assert_cmpint (s->room, >=, 0);
			g_assert_cmpint (lrg_procgen_grid_room_at (grid, s->x, s->y), ==, s->room);
		}
	}
	g_assert_cmpuint (goblins, ==, 6);
	g_assert_cmpuint (chests, ==, 3);

	/* entities through the callback (chests skipped) */
	g_assert_cmpuint (lrg_procgen_grid_spawn_entities (grid, world, NULL, 16.0f, spawn_cb, &count), ==, 6);
	g_assert_cmpuint (count.calls, ==, 9);
	g_assert_cmpuint (lrg_world_get_object_count (world), ==, 6);
	g_assert_nonnull (lrg_world_find_by_tag (world, "monster"));

	/* plain objects tagged with the kind; the registry is used for known kinds */
	lrg_registry_register (registry, "goblin", LRG_TYPE_GAME_OBJECT);
	g_assert_cmpuint (lrg_procgen_grid_spawn_entities (grid, plain, registry, 1.0f, NULL, NULL), ==, 9);
	g_assert_cmpuint (lrg_world_get_object_count (plain), ==, 9);
	g_assert_nonnull (lrg_world_find_by_tag (plain, "goblin"));
	g_assert_nonnull (lrg_world_find_by_tag (plain, "chest"));
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/procgen/grid/basics", test_grid_basics);
	g_test_add_func ("/procgen/grid/regions", test_grid_regions);
	g_test_add_func ("/procgen/grid/start-exit", test_grid_start_exit);
	g_test_add_func ("/procgen/grid/reference-fuzz", test_grid_reference_fuzz);
	g_test_add_func ("/procgen/bsp/determinism", test_bsp_determinism);
	g_test_add_func ("/procgen/bsp/invariants", test_bsp_invariants);
	g_test_add_func ("/procgen/bsp/bad-params", test_bsp_bad_params);
	g_test_add_func ("/procgen/caves", test_caves);
	g_test_add_func ("/procgen/drunkard", test_drunkard);
	g_test_add_func ("/procgen/noise", test_noise);
	g_test_add_func ("/procgen/wfc/learned", test_wfc_learned);
	g_test_add_func ("/procgen/wfc/checkerboard", test_wfc_checkerboard);
	g_test_add_func ("/procgen/wfc/backtracking", test_wfc_backtracking);
	g_test_add_func ("/procgen/wfc/contradiction", test_wfc_contradiction);
	g_test_add_func ("/procgen/tile-rules", test_tile_rules);
	g_test_add_func ("/procgen/nav-grid", test_nav_grid);
	g_test_add_func ("/procgen/dungeon/yaml-round-trip", test_dungeon_yaml_round_trip);
	g_test_add_func ("/procgen/dungeon/yaml-wfc", test_dungeon_yaml_wfc);
	g_test_add_func ("/procgen/dungeon/yaml-errors", test_dungeon_yaml_errors);
	g_test_add_func ("/procgen/dungeon/example-file", test_dungeon_example_file);
	g_test_add_func ("/procgen/dungeon/spawns", test_dungeon_spawns);
	g_test_add_func ("/procgen/data-loader", test_data_loader);

	return g_test_run ();
}
