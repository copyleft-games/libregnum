/* lrg-procgen-grid.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Cell grid produced by the procedural generators, its metadata and the
 * shared post-processing passes (regions, connectivity, start/exit,
 * spawn points).
 *
 * Every pass here is integer-only and scans in row-major order so the same
 * input always gives the same output on every platform.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PROCGEN

#include "lrg-procgen-grid.h"
#include "../ecs/lrg-game-object.h"
#include "../lrg-log.h"

#include <string.h>

/* ==========================================================================
 * Boxed types
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgProcgenRoom, lrg_procgen_room,
                     lrg_procgen_room_copy, lrg_procgen_room_free)

LrgProcgenRoom *
lrg_procgen_room_new (gint x,
                      gint y,
                      gint width,
                      gint height)
{
	LrgProcgenRoom *self;

	self = g_new0 (LrgProcgenRoom, 1);
	self->x = x;
	self->y = y;
	self->width = width;
	self->height = height;
	return self;
}

LrgProcgenRoom *
lrg_procgen_room_copy (const LrgProcgenRoom *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return lrg_procgen_room_new (self->x, self->y, self->width, self->height);
}

void
lrg_procgen_room_free (LrgProcgenRoom *self)
{
	g_free (self);
}

gboolean
lrg_procgen_room_contains (const LrgProcgenRoom *self,
                           gint                  x,
                           gint                  y)
{
	g_return_val_if_fail (self != NULL, FALSE);
	return x >= self->x && y >= self->y &&
	       x < self->x + self->width && y < self->y + self->height;
}

void
lrg_procgen_room_get_center (const LrgProcgenRoom *self,
                             gint                 *out_x,
                             gint                 *out_y)
{
	g_return_if_fail (self != NULL);
	if (out_x != NULL)
		*out_x = self->x + self->width / 2;
	if (out_y != NULL)
		*out_y = self->y + self->height / 2;
}

G_DEFINE_BOXED_TYPE (LrgProcgenSpawn, lrg_procgen_spawn,
                     lrg_procgen_spawn_copy, lrg_procgen_spawn_free)

LrgProcgenSpawn *
lrg_procgen_spawn_new (const gchar *kind,
                       gint         x,
                       gint         y,
                       gint         room)
{
	LrgProcgenSpawn *self;

	self = g_new0 (LrgProcgenSpawn, 1);
	self->kind = g_strdup (kind != NULL ? kind : "");
	self->x = x;
	self->y = y;
	self->room = room;
	return self;
}

LrgProcgenSpawn *
lrg_procgen_spawn_copy (const LrgProcgenSpawn *self)
{
	g_return_val_if_fail (self != NULL, NULL);
	return lrg_procgen_spawn_new (self->kind, self->x, self->y, self->room);
}

void
lrg_procgen_spawn_free (LrgProcgenSpawn *self)
{
	if (self == NULL)
		return;
	g_free (self->kind);
	g_free (self);
}

/* ==========================================================================
 * LrgProcgenGrid
 * ========================================================================== */

struct _LrgProcgenGrid
{
	GObject    parent_instance;

	guint      width;
	guint      height;
	guint8    *cells;         /* row-major tags */
	gint      *regions;       /* row-major region index, -1 = none */
	GArray    *region_sizes;  /* guint per region */
	gboolean   walkable[256]; /* walkable flag per tag */
	gfloat    *heights;       /* optional heightmap, NULL until set */
	GPtrArray *rooms;         /* LrgProcgenRoom */
	GPtrArray *spawns;        /* LrgProcgenSpawn */
	gboolean   has_start;
	gint       start_x;
	gint       start_y;
	gboolean   has_exit;
	gint       exit_x;
	gint       exit_y;
};

G_DEFINE_FINAL_TYPE (LrgProcgenGrid, lrg_procgen_grid, G_TYPE_OBJECT)

/* the 4-connected neighbour offsets, in the order up, right, down, left */
static const gint dir_dx[4] = { 0, 1, 0, -1 };
static const gint dir_dy[4] = { -1, 0, 1, 0 };

static void
lrg_procgen_grid_finalize (GObject *object)
{
	LrgProcgenGrid *self = LRG_PROCGEN_GRID (object);

	g_clear_pointer (&self->cells, g_free);
	g_clear_pointer (&self->regions, g_free);
	g_clear_pointer (&self->heights, g_free);
	g_clear_pointer (&self->region_sizes, g_array_unref);
	g_clear_pointer (&self->rooms, g_ptr_array_unref);
	g_clear_pointer (&self->spawns, g_ptr_array_unref);
	G_OBJECT_CLASS (lrg_procgen_grid_parent_class)->finalize (object);
}

static void
lrg_procgen_grid_class_init (LrgProcgenGridClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_procgen_grid_finalize;
}

static void
lrg_procgen_grid_init (LrgProcgenGrid *self)
{
	self->region_sizes = g_array_new (FALSE, TRUE, sizeof (guint));
	self->rooms = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_procgen_room_free);
	self->spawns = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_procgen_spawn_free);
	self->walkable[LRG_PROCGEN_CELL_FLOOR] = TRUE;
	self->walkable[LRG_PROCGEN_CELL_CORRIDOR] = TRUE;
	self->walkable[LRG_PROCGEN_CELL_DOOR] = TRUE;
}

/* the row-major index of an in-bounds cell */
static inline gsize
cell_index (LrgProcgenGrid *self,
            gint            x,
            gint            y)
{
	return (gsize) y * self->width + (gsize) x;
}

static inline gboolean
in_bounds (LrgProcgenGrid *self,
           gint            x,
           gint            y)
{
	return x >= 0 && y >= 0 && (guint) x < self->width && (guint) y < self->height;
}

static inline gsize
cell_count (LrgProcgenGrid *self)
{
	return (gsize) self->width * self->height;
}

/* cells changed: the region labels are stale */
static void
invalidate_regions (LrgProcgenGrid *self)
{
	g_array_set_size (self->region_sizes, 0);
	memset (self->regions, 0xff, cell_count (self) * sizeof (gint));
}

LrgProcgenGrid *
lrg_procgen_grid_new (guint width,
                      guint height)
{
	LrgProcgenGrid *self;

	g_return_val_if_fail (width > 0 && height > 0, NULL);
	g_return_val_if_fail ((guint64) width * height <= G_MAXINT32, NULL);

	self = g_object_new (LRG_TYPE_PROCGEN_GRID, NULL);
	self->width = width;
	self->height = height;
	self->cells = g_new0 (guint8, cell_count (self));
	self->regions = g_new (gint, cell_count (self));
	invalidate_regions (self);
	return self;
}

/* the tag of a text grid character, or -1 */
static gint
char_to_tag (gchar c)
{
	switch (c)
	{
	case '#': return LRG_PROCGEN_CELL_WALL;
	case '.': return LRG_PROCGEN_CELL_FLOOR;
	case ',': return LRG_PROCGEN_CELL_CORRIDOR;
	case '+': return LRG_PROCGEN_CELL_DOOR;
	case '~': return LRG_PROCGEN_CELL_WATER;
	case ' ': return LRG_PROCGEN_CELL_VOID;
	default:
		if (c >= 'a' && c <= 'z')
			return LRG_PROCGEN_CELL_CUSTOM + (c - 'a');
		return -1;
	}
}

/* the text grid character of a tag */
static gchar
tag_to_char (guint8 tag)
{
	switch (tag)
	{
	case LRG_PROCGEN_CELL_WALL:     return '#';
	case LRG_PROCGEN_CELL_FLOOR:    return '.';
	case LRG_PROCGEN_CELL_CORRIDOR: return ',';
	case LRG_PROCGEN_CELL_DOOR:     return '+';
	case LRG_PROCGEN_CELL_WATER:    return '~';
	case LRG_PROCGEN_CELL_VOID:     return ' ';
	default:
		if (tag >= LRG_PROCGEN_CELL_CUSTOM && tag < LRG_PROCGEN_CELL_CUSTOM + 26)
			return (gchar) ('a' + (tag - LRG_PROCGEN_CELL_CUSTOM));
		return '?';
	}
}

LrgProcgenGrid *
lrg_procgen_grid_new_from_string (const gchar  *text,
                                  GError      **error)
{
	g_auto(GStrv) lines = NULL;
	g_autoptr(LrgProcgenGrid) grid = NULL;
	guint n_lines;
	guint width;
	guint y;
	guint x;

	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* split into rows, dropping one trailing empty line */
	lines = g_strsplit (text, "\n", -1);
	n_lines = g_strv_length (lines);
	if (n_lines > 0 && lines[n_lines - 1][0] == '\0')
		n_lines--;
	if (n_lines == 0 || lines[0][0] == '\0')
	{
		g_set_error_literal (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG,
		                     "empty grid text");
		return NULL;
	}

	width = (guint) strlen (lines[0]);
	grid = lrg_procgen_grid_new (width, n_lines);
	for (y = 0; y < n_lines; y++)
	{
		if (strlen (lines[y]) != width)
		{
			g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG,
			             "grid row %u has %u cells, expected %u",
			             y, (guint) strlen (lines[y]), width);
			return NULL;
		}
		for (x = 0; x < width; x++)
		{
			gint tag = char_to_tag (lines[y][x]);

			if (tag < 0)
			{
				g_set_error (error, LRG_PROCGEN_ERROR, LRG_PROCGEN_ERROR_INVALID_CONFIG,
				             "unknown grid character '%c' at %u,%u", lines[y][x], x, y);
				return NULL;
			}
			grid->cells[cell_index (grid, (gint) x, (gint) y)] = (guint8) tag;
		}
	}
	return g_steal_pointer (&grid);
}

LrgProcgenGrid *
lrg_procgen_grid_copy (LrgProcgenGrid *self)
{
	LrgProcgenGrid *copy;
	guint i;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), NULL);

	copy = lrg_procgen_grid_new (self->width, self->height);
	memcpy (copy->cells, self->cells, cell_count (self));
	memcpy (copy->regions, self->regions, cell_count (self) * sizeof (gint));
	g_array_append_vals (copy->region_sizes, self->region_sizes->data, self->region_sizes->len);
	memcpy (copy->walkable, self->walkable, sizeof self->walkable);
	if (self->heights != NULL)
		copy->heights = g_memdup2 (self->heights, cell_count (self) * sizeof (gfloat));
	for (i = 0; i < self->rooms->len; i++)
		g_ptr_array_add (copy->rooms, lrg_procgen_room_copy (g_ptr_array_index (self->rooms, i)));
	for (i = 0; i < self->spawns->len; i++)
		g_ptr_array_add (copy->spawns, lrg_procgen_spawn_copy (g_ptr_array_index (self->spawns, i)));
	copy->has_start = self->has_start;
	copy->start_x = self->start_x;
	copy->start_y = self->start_y;
	copy->has_exit = self->has_exit;
	copy->exit_x = self->exit_x;
	copy->exit_y = self->exit_y;
	return copy;
}

/* ==========================================================================
 * Cells
 * ========================================================================== */

guint
lrg_procgen_grid_get_width (LrgProcgenGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	return self->width;
}

guint
lrg_procgen_grid_get_height (LrgProcgenGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	return self->height;
}

gboolean
lrg_procgen_grid_in_bounds (LrgProcgenGrid *self,
                            gint            x,
                            gint            y)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), FALSE);
	return in_bounds (self, x, y);
}

guint8
lrg_procgen_grid_get_cell (LrgProcgenGrid *self,
                           gint            x,
                           gint            y)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), LRG_PROCGEN_CELL_WALL);
	if (!in_bounds (self, x, y))
		return LRG_PROCGEN_CELL_WALL;
	return self->cells[cell_index (self, x, y)];
}

void
lrg_procgen_grid_set_cell (LrgProcgenGrid *self,
                           gint            x,
                           gint            y,
                           guint8          tag)
{
	g_return_if_fail (LRG_IS_PROCGEN_GRID (self));
	if (in_bounds (self, x, y))
		self->cells[cell_index (self, x, y)] = tag;
}

void
lrg_procgen_grid_fill (LrgProcgenGrid *self,
                       guint8          tag)
{
	g_return_if_fail (LRG_IS_PROCGEN_GRID (self));
	memset (self->cells, tag, cell_count (self));
	invalidate_regions (self);
}

void
lrg_procgen_grid_fill_rect (LrgProcgenGrid *self,
                            gint            x,
                            gint            y,
                            gint            width,
                            gint            height,
                            guint8          tag)
{
	gint x0;
	gint y0;
	gint x1;
	gint y1;
	gint cy;

	g_return_if_fail (LRG_IS_PROCGEN_GRID (self));

	/* clip the rectangle to the grid */
	x0 = MAX (x, 0);
	y0 = MAX (y, 0);
	x1 = MIN (x + width, (gint) self->width);
	y1 = MIN (y + height, (gint) self->height);
	for (cy = y0; cy < y1; cy++)
		if (x1 > x0)
			memset (self->cells + cell_index (self, x0, cy), tag, (gsize) (x1 - x0));
}

guint
lrg_procgen_grid_count_cells (LrgProcgenGrid *self,
                              guint8          tag)
{
	gsize i;
	guint n = 0;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	for (i = 0; i < cell_count (self); i++)
		if (self->cells[i] == tag)
			n++;
	return n;
}

const guint8 *
lrg_procgen_grid_get_cells (LrgProcgenGrid *self,
                            gsize          *out_len)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), NULL);
	if (out_len != NULL)
		*out_len = cell_count (self);
	return self->cells;
}

/* ==========================================================================
 * Walkability
 * ========================================================================== */

void
lrg_procgen_grid_set_tag_walkable (LrgProcgenGrid *self,
                                   guint8          tag,
                                   gboolean        walkable)
{
	g_return_if_fail (LRG_IS_PROCGEN_GRID (self));
	self->walkable[tag] = walkable ? TRUE : FALSE;
}

gboolean
lrg_procgen_grid_is_tag_walkable (LrgProcgenGrid *self,
                                  guint8          tag)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), FALSE);
	return self->walkable[tag];
}

/* in-bounds and walkable, without type checks (hot loops) */
static inline gboolean
walkable_at (LrgProcgenGrid *self,
             gint            x,
             gint            y)
{
	return in_bounds (self, x, y) && self->walkable[self->cells[cell_index (self, x, y)]];
}

gboolean
lrg_procgen_grid_is_walkable (LrgProcgenGrid *self,
                              gint            x,
                              gint            y)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), FALSE);
	return walkable_at (self, x, y);
}

guint
lrg_procgen_grid_count_walkable (LrgProcgenGrid *self)
{
	gsize i;
	guint n = 0;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	for (i = 0; i < cell_count (self); i++)
		if (self->walkable[self->cells[i]])
			n++;
	return n;
}

/* ==========================================================================
 * Regions
 * ========================================================================== */

guint
lrg_procgen_grid_label_regions (LrgProcgenGrid *self)
{
	g_autofree guint32 *queue = NULL;
	gsize n;
	gsize start;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);

	invalidate_regions (self);
	n = cell_count (self);
	queue = g_new (guint32, n);

	/*
	 * Scan in row-major order; each unlabelled walkable cell seeds a
	 * breadth-first flood fill that labels its whole 4-connected region.
	 */
	for (start = 0; start < n; start++)
	{
		gsize head = 0;
		gsize tail = 0;
		gint region;
		guint size = 0;

		if (self->regions[start] >= 0 || !self->walkable[self->cells[start]])
			continue;

		region = (gint) self->region_sizes->len;
		self->regions[start] = region;
		queue[tail++] = (guint32) start;
		while (head < tail)
		{
			guint32 c = queue[head++];
			gint cx = (gint) (c % self->width);
			gint cy = (gint) (c / self->width);
			guint d;

			size++;
			for (d = 0; d < 4; d++)
			{
				gint nx = cx + dir_dx[d];
				gint ny = cy + dir_dy[d];
				gsize ni;

				if (!walkable_at (self, nx, ny))
					continue;
				ni = cell_index (self, nx, ny);
				if (self->regions[ni] >= 0)
					continue;
				self->regions[ni] = region;
				queue[tail++] = (guint32) ni;
			}
		}
		g_array_append_val (self->region_sizes, size);
	}
	return self->region_sizes->len;
}

guint
lrg_procgen_grid_get_region_count (LrgProcgenGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	return self->region_sizes->len;
}

gint
lrg_procgen_grid_get_region (LrgProcgenGrid *self,
                             gint            x,
                             gint            y)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), -1);
	if (!in_bounds (self, x, y))
		return -1;
	return self->regions[cell_index (self, x, y)];
}

guint
lrg_procgen_grid_get_region_size (LrgProcgenGrid *self,
                                  guint           region)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	if (region >= self->region_sizes->len)
		return 0;
	return g_array_index (self->region_sizes, guint, region);
}

gboolean
lrg_procgen_grid_is_connected (LrgProcgenGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), FALSE);
	return lrg_procgen_grid_label_regions (self) <= 1;
}

/* overwrite every cell of the regions flagged in @drop, then relabel */
static void
drop_regions (LrgProcgenGrid *self,
              const gboolean *drop,
              guint8          fill_tag)
{
	gsize i;

	for (i = 0; i < cell_count (self); i++)
		if (self->regions[i] >= 0 && drop[self->regions[i]])
			self->cells[i] = fill_tag;
	lrg_procgen_grid_label_regions (self);
}

guint
lrg_procgen_grid_cull_regions (LrgProcgenGrid *self,
                               guint           min_size,
                               guint8          fill_tag)
{
	g_autofree gboolean *drop = NULL;
	guint n;
	guint r;
	guint removed = 0;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);

	n = lrg_procgen_grid_label_regions (self);
	if (n == 0)
		return 0;
	drop = g_new0 (gboolean, n);
	for (r = 0; r < n; r++)
		if (g_array_index (self->region_sizes, guint, r) < min_size)
		{
			drop[r] = TRUE;
			removed++;
		}
	if (removed > 0)
		drop_regions (self, drop, fill_tag);
	return removed;
}

guint
lrg_procgen_grid_keep_largest_region (LrgProcgenGrid *self,
                                      guint8          fill_tag)
{
	g_autofree gboolean *drop = NULL;
	guint n;
	guint r;
	guint best = 0;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);

	n = lrg_procgen_grid_label_regions (self);
	if (n <= 1)
		return 0;
	for (r = 1; r < n; r++)
		if (g_array_index (self->region_sizes, guint, r) >
		    g_array_index (self->region_sizes, guint, best))
			best = r;
	drop = g_new (gboolean, n);
	for (r = 0; r < n; r++)
		drop[r] = r != best;
	drop_regions (self, drop, fill_tag);
	return n - 1;
}

guint
lrg_procgen_grid_connect_regions (LrgProcgenGrid *self,
                                  guint8          corridor_tag)
{
	g_autofree guint32 *queue = NULL;
	g_autofree gint32 *parent = NULL;
	gsize n;
	guint carved = 0;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);

	n = cell_count (self);
	queue = g_new (guint32, n);
	parent = g_new (gint32, n);

	/*
	 * Each round: a multi-source BFS from every cell of region 0 walks
	 * through any interior cell until it reaches a cell of another region;
	 * the parent chain back to region 0 is the shortest corridor, which is
	 * carved. Repeating until one region remains connects everything with
	 * at most (regions - 1) corridors.
	 */
	while (lrg_procgen_grid_label_regions (self) > 1)
	{
		gsize head = 0;
		gsize tail = 0;
		gsize i;
		gint32 hit = -1;

		for (i = 0; i < n; i++)
		{
			parent[i] = -2; /* unvisited */
			if (self->regions[i] == 0)
			{
				parent[i] = -1; /* a source */
				queue[tail++] = (guint32) i;
			}
		}

		while (head < tail && hit < 0)
		{
			guint32 c = queue[head++];
			gint cx = (gint) (c % self->width);
			gint cy = (gint) (c / self->width);
			guint d;

			for (d = 0; d < 4; d++)
			{
				gint nx = cx + dir_dx[d];
				gint ny = cy + dir_dy[d];
				gsize ni;

				if (!in_bounds (self, nx, ny))
					continue;
				ni = cell_index (self, nx, ny);
				if (parent[ni] != -2)
					continue;
				/* keep the outer border solid: only walk onto it into a region */
				if ((nx == 0 || ny == 0 || nx == (gint) self->width - 1 ||
				     ny == (gint) self->height - 1) && self->regions[ni] < 0)
					continue;
				parent[ni] = (gint32) c;
				if (self->regions[ni] > 0)
				{
					hit = (gint32) ni;
					break;
				}
				queue[tail++] = (guint32) ni;
			}
		}

		if (hit < 0)
		{
			/* the other regions cannot be reached (a 1-cell-thick grid) */
			lrg_debug (LRG_LOG_DOMAIN_PROCGEN, "connect_regions: unreachable region");
			break;
		}

		/* carve the corridor back from the hit cell to region 0 */
		for (i = (gsize) parent[hit]; parent[i] >= 0; i = (gsize) parent[i])
			if (!self->walkable[self->cells[i]])
				self->cells[i] = corridor_tag;
		carved++;

		/* a corridor tag that is not walkable could never connect */
		if (!self->walkable[corridor_tag])
			break;
	}
	return carved;
}

/* ==========================================================================
 * Distances
 * ========================================================================== */

/* fills @dist (cell_count entries) with BFS steps from a cell, -1 elsewhere */
static void
fill_distances (LrgProcgenGrid *self,
                gint            from_x,
                gint            from_y,
                gint           *dist,
                guint32        *queue)
{
	gsize n = cell_count (self);
	gsize head = 0;
	gsize tail = 0;
	gsize i;

	for (i = 0; i < n; i++)
		dist[i] = -1;
	if (!walkable_at (self, from_x, from_y))
		return;
	i = cell_index (self, from_x, from_y);
	dist[i] = 0;
	queue[tail++] = (guint32) i;
	while (head < tail)
	{
		guint32 c = queue[head++];
		gint cx = (gint) (c % self->width);
		gint cy = (gint) (c / self->width);
		guint d;

		for (d = 0; d < 4; d++)
		{
			gint nx = cx + dir_dx[d];
			gint ny = cy + dir_dy[d];
			gsize ni;

			if (!walkable_at (self, nx, ny))
				continue;
			ni = cell_index (self, nx, ny);
			if (dist[ni] >= 0)
				continue;
			dist[ni] = dist[c] + 1;
			queue[tail++] = (guint32) ni;
		}
	}
}

gint *
lrg_procgen_grid_distance_map (LrgProcgenGrid *self,
                               gint            from_x,
                               gint            from_y,
                               gsize          *out_len)
{
	g_autofree guint32 *queue = NULL;
	gint *dist;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), NULL);

	dist = g_new (gint, cell_count (self));
	queue = g_new (guint32, cell_count (self));
	fill_distances (self, from_x, from_y, dist, queue);
	if (out_len != NULL)
		*out_len = cell_count (self);
	return dist;
}

gint
lrg_procgen_grid_path_distance (LrgProcgenGrid *self,
                                gint            x0,
                                gint            y0,
                                gint            x1,
                                gint            y1)
{
	g_autofree gint *dist = NULL;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), -1);
	if (!in_bounds (self, x1, y1))
		return -1;
	dist = lrg_procgen_grid_distance_map (self, x0, y0, NULL);
	return dist[cell_index (self, x1, y1)];
}

/* ==========================================================================
 * Rooms
 * ========================================================================== */

guint
lrg_procgen_grid_add_room (LrgProcgenGrid       *self,
                           const LrgProcgenRoom *room)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	g_return_val_if_fail (room != NULL, 0);
	g_ptr_array_add (self->rooms, lrg_procgen_room_copy (room));
	return self->rooms->len - 1;
}

guint
lrg_procgen_grid_get_room_count (LrgProcgenGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	return self->rooms->len;
}

const LrgProcgenRoom *
lrg_procgen_grid_get_room (LrgProcgenGrid *self,
                           guint           index)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), NULL);
	if (index >= self->rooms->len)
		return NULL;
	return g_ptr_array_index (self->rooms, index);
}

gint
lrg_procgen_grid_room_at (LrgProcgenGrid *self,
                          gint            x,
                          gint            y)
{
	guint i;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), -1);
	for (i = 0; i < self->rooms->len; i++)
		if (lrg_procgen_room_contains (g_ptr_array_index (self->rooms, i), x, y))
			return (gint) i;
	return -1;
}

/* ==========================================================================
 * Start and exit
 * ========================================================================== */

void
lrg_procgen_grid_set_start (LrgProcgenGrid *self,
                            gint            x,
                            gint            y)
{
	g_return_if_fail (LRG_IS_PROCGEN_GRID (self));
	self->has_start = TRUE;
	self->start_x = x;
	self->start_y = y;
}

gboolean
lrg_procgen_grid_get_start (LrgProcgenGrid *self,
                            gint           *out_x,
                            gint           *out_y)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), FALSE);
	if (out_x != NULL)
		*out_x = self->start_x;
	if (out_y != NULL)
		*out_y = self->start_y;
	return self->has_start;
}

void
lrg_procgen_grid_set_exit (LrgProcgenGrid *self,
                           gint            x,
                           gint            y)
{
	g_return_if_fail (LRG_IS_PROCGEN_GRID (self));
	self->has_exit = TRUE;
	self->exit_x = x;
	self->exit_y = y;
}

gboolean
lrg_procgen_grid_get_exit (LrgProcgenGrid *self,
                           gint           *out_x,
                           gint           *out_y)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), FALSE);
	if (out_x != NULL)
		*out_x = self->exit_x;
	if (out_y != NULL)
		*out_y = self->exit_y;
	return self->has_exit;
}

/* the index of the farthest cell of @dist (the first one on a tie) */
static gsize
farthest_cell (const gint *dist,
               gsize       n)
{
	gsize i;
	gsize best = 0;
	gint best_d = -1;

	for (i = 0; i < n; i++)
		if (dist[i] > best_d)
		{
			best_d = dist[i];
			best = i;
		}
	return best;
}

gboolean
lrg_procgen_grid_place_start_exit (LrgProcgenGrid *self)
{
	g_autofree gint *dist = NULL;
	g_autofree guint32 *queue = NULL;
	guint n_regions;
	guint r;
	guint best = 0;
	gsize n;
	gsize i;
	gsize seed = 0;
	gsize a;
	gsize b;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), FALSE);

	n_regions = lrg_procgen_grid_label_regions (self);
	if (n_regions == 0)
		return FALSE;

	/* the largest region, and its first cell */
	for (r = 1; r < n_regions; r++)
		if (g_array_index (self->region_sizes, guint, r) >
		    g_array_index (self->region_sizes, guint, best))
			best = r;
	n = cell_count (self);
	for (i = 0; i < n; i++)
		if (self->regions[i] == (gint) best)
		{
			seed = i;
			break;
		}

	/* double sweep: farthest from the seed, then farthest from that */
	dist = g_new (gint, n);
	queue = g_new (guint32, n);
	fill_distances (self, (gint) (seed % self->width), (gint) (seed / self->width), dist, queue);
	a = farthest_cell (dist, n);
	fill_distances (self, (gint) (a % self->width), (gint) (a / self->width), dist, queue);
	b = farthest_cell (dist, n);

	lrg_procgen_grid_set_start (self, (gint) (a % self->width), (gint) (a / self->width));
	lrg_procgen_grid_set_exit (self, (gint) (b % self->width), (gint) (b / self->width));
	return TRUE;
}

/* ==========================================================================
 * Heights
 * ========================================================================== */

gboolean
lrg_procgen_grid_has_heights (LrgProcgenGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), FALSE);
	return self->heights != NULL;
}

gfloat
lrg_procgen_grid_get_height_at (LrgProcgenGrid *self,
                                gint            x,
                                gint            y)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0.0f);
	if (self->heights == NULL || !in_bounds (self, x, y))
		return 0.0f;
	return self->heights[cell_index (self, x, y)];
}

void
lrg_procgen_grid_set_height_at (LrgProcgenGrid *self,
                                gint            x,
                                gint            y,
                                gfloat          height)
{
	g_return_if_fail (LRG_IS_PROCGEN_GRID (self));
	if (!in_bounds (self, x, y))
		return;
	if (self->heights == NULL)
		self->heights = g_new0 (gfloat, cell_count (self));
	self->heights[cell_index (self, x, y)] = height;
}

/* ==========================================================================
 * Spawn points
 * ========================================================================== */

void
lrg_procgen_grid_add_spawn (LrgProcgenGrid        *self,
                            const LrgProcgenSpawn *spawn)
{
	g_return_if_fail (LRG_IS_PROCGEN_GRID (self));
	g_return_if_fail (spawn != NULL);
	g_ptr_array_add (self->spawns, lrg_procgen_spawn_copy (spawn));
}

guint
lrg_procgen_grid_get_spawn_count (LrgProcgenGrid *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	return self->spawns->len;
}

const LrgProcgenSpawn *
lrg_procgen_grid_get_spawn (LrgProcgenGrid *self,
                            guint           index)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), NULL);
	if (index >= self->spawns->len)
		return NULL;
	return g_ptr_array_index (self->spawns, index);
}

void
lrg_procgen_grid_clear_spawns (LrgProcgenGrid *self)
{
	g_return_if_fail (LRG_IS_PROCGEN_GRID (self));
	g_ptr_array_set_size (self->spawns, 0);
}

guint
lrg_procgen_grid_place_spawns (LrgProcgenGrid  *self,
                               LrgRandomStream *rng,
                               const gchar     *kind,
                               guint            count,
                               guint            min_distance,
                               gboolean         rooms_only)
{
	g_autofree gint *dist = NULL;
	g_autofree gboolean *taken = NULL;
	g_autoptr(GArray) candidates = NULL;
	gsize n;
	gsize i;
	guint placed = 0;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	g_return_val_if_fail (LRG_IS_RANDOM_STREAM (rng), 0);
	g_return_val_if_fail (kind != NULL, 0);

	n = cell_count (self);
	if (self->has_start)
		dist = lrg_procgen_grid_distance_map (self, self->start_x, self->start_y, NULL);

	/* cells already used by the start, the exit and earlier spawn points */
	taken = g_new0 (gboolean, n);
	if (self->has_start && in_bounds (self, self->start_x, self->start_y))
		taken[cell_index (self, self->start_x, self->start_y)] = TRUE;
	if (self->has_exit && in_bounds (self, self->exit_x, self->exit_y))
		taken[cell_index (self, self->exit_x, self->exit_y)] = TRUE;
	for (i = 0; i < self->spawns->len; i++)
	{
		LrgProcgenSpawn *s = g_ptr_array_index (self->spawns, i);

		if (in_bounds (self, s->x, s->y))
			taken[cell_index (self, s->x, s->y)] = TRUE;
	}

	/* collect the candidates in row-major order */
	candidates = g_array_new (FALSE, FALSE, sizeof (guint32));
	for (i = 0; i < n; i++)
	{
		gint x = (gint) (i % self->width);
		gint y = (gint) (i / self->width);
		guint32 v = (guint32) i;

		if (taken[i] || !self->walkable[self->cells[i]])
			continue;
		if (dist != NULL && (dist[i] < 0 || (guint) dist[i] < min_distance))
			continue;
		if (rooms_only && lrg_procgen_grid_room_at (self, x, y) < 0)
			continue;
		g_array_append_val (candidates, v);
	}

	/* a partial Fisher-Yates shuffle picks distinct cells */
	while (placed < count && placed < candidates->len)
	{
		guint32 j = placed + lrg_random_stream_bounded (rng, candidates->len - placed);
		guint32 c = g_array_index (candidates, guint32, j);
		gint x = (gint) (c % self->width);
		gint y = (gint) (c / self->width);

		g_array_index (candidates, guint32, j) = g_array_index (candidates, guint32, placed);
		g_array_index (candidates, guint32, placed) = c;
		g_ptr_array_add (self->spawns,
		                 lrg_procgen_spawn_new (kind, x, y, lrg_procgen_grid_room_at (self, x, y)));
		placed++;
	}
	return placed;
}

guint
lrg_procgen_grid_spawn_entities (LrgProcgenGrid      *self,
                                 LrgWorld            *world,
                                 LrgRegistry         *registry,
                                 gfloat               cell_size,
                                 LrgProcgenSpawnFunc  func,
                                 gpointer             user_data)
{
	guint i;
	guint created = 0;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);
	g_return_val_if_fail (world == NULL || LRG_IS_WORLD (world), 0);
	g_return_val_if_fail (registry == NULL || LRG_IS_REGISTRY (registry), 0);

	for (i = 0; i < self->spawns->len; i++)
	{
		LrgProcgenSpawn *spawn = g_ptr_array_index (self->spawns, i);
		g_autoptr(LrgGameObject) object = NULL;
		gfloat wx = ((gfloat) spawn->x + 0.5f) * cell_size;
		gfloat wy = ((gfloat) spawn->y + 0.5f) * cell_size;

		/* the callback, then the registry, then a plain game object */
		if (func != NULL)
			object = func (self, spawn, wx, wy, user_data);
		else if (registry != NULL && lrg_registry_is_registered (registry, spawn->kind) &&
		         g_type_is_a (lrg_registry_lookup (registry, spawn->kind), LRG_TYPE_GAME_OBJECT))
		{
			object = LRG_GAME_OBJECT (lrg_registry_create (registry, spawn->kind, NULL));
			if (object != NULL)
				grl_entity_set_position_xy (GRL_ENTITY (object), wx, wy);
		}
		else
			object = lrg_game_object_new_at (wx, wy);

		if (object == NULL)
			continue;
		if (grl_entity_get_tag (GRL_ENTITY (object)) == NULL)
			grl_entity_set_tag (GRL_ENTITY (object), spawn->kind);
		if (world != NULL)
			lrg_world_add_object (world, object);
		created++;
	}
	return created;
}

/* ==========================================================================
 * Output
 * ========================================================================== */

gchar *
lrg_procgen_grid_to_string (LrgProcgenGrid *self)
{
	GString *text;
	guint x;
	guint y;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), NULL);

	text = g_string_sized_new (cell_count (self) + self->height);
	for (y = 0; y < self->height; y++)
	{
		for (x = 0; x < self->width; x++)
			g_string_append_c (text, tag_to_char (self->cells[cell_index (self, (gint) x, (gint) y)]));
		g_string_append_c (text, '\n');
	}
	return g_string_free (text, FALSE);
}

guint64
lrg_procgen_grid_hash (LrgProcgenGrid *self)
{
	guint64 h = G_GUINT64_CONSTANT (14695981039346656037);
	guint64 prime = G_GUINT64_CONSTANT (1099511628211);
	guint32 dims[2];
	gsize i;
	guint b;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), 0);

	/* the size as little-endian bytes, then every cell */
	dims[0] = self->width;
	dims[1] = self->height;
	for (i = 0; i < 2; i++)
		for (b = 0; b < 4; b++)
		{
			h ^= (dims[i] >> (8 * b)) & 0xff;
			h *= prime;
		}
	for (i = 0; i < cell_count (self); i++)
	{
		h ^= self->cells[i];
		h *= prime;
	}
	return h;
}

LrgNavGrid *
lrg_procgen_grid_to_nav_grid (LrgProcgenGrid *self)
{
	LrgNavGrid *nav;
	guint x;
	guint y;

	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (self), NULL);

	nav = lrg_nav_grid_new (self->width, self->height);
	for (y = 0; y < self->height; y++)
		for (x = 0; x < self->width; x++)
			if (!walkable_at (self, (gint) x, (gint) y))
				lrg_nav_grid_set_blocked (nav, (gint) x, (gint) y, TRUE);
	return nav;
}
