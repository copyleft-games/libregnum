/* lrg-node-tilemap.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Editable tile grid carried by a tilemap node's visual. Each layer is a
 * flat row-major array of tile ids; all layers share the grid size.
 */

#include "lrg-node-tilemap.h"
#include "../tilemap/lrg-tilemap.h"
#include "../tilemap/lrg-tilemap-layer.h"
#include <math.h>
#include <string.h>

typedef struct
{
	gchar    *name;
	gboolean  visible;
	guint    *tiles;    /* width * height cells, row-major */
} TileLayer;

/**
 * LrgNodeTilemap:
 *
 * The tile grid of a tilemap node. See the header for the geometry.
 */
struct _LrgNodeTilemap
{
	GObject parent_instance;

	guint      width;
	guint      height;
	guint      tile_width;   /* tileset slicing, pixels */
	guint      tile_height;
	GPtrArray *layers;       /* TileLayer*, bottom first */

	/* 0.9.0 grid layout, applied by lrg_node_tilemap_to_runtime() */
	LrgTilemapOrientation  orientation;
	LrgTilemapStaggerAxis  stagger_axis;
	LrgTilemapStaggerIndex stagger_index;
	guint                  hex_side_length;
	guint                  cell_width;
	guint                  cell_height;
};

G_DEFINE_FINAL_TYPE (LrgNodeTilemap, lrg_node_tilemap, G_TYPE_OBJECT)

static void
tile_layer_free (gpointer data)
{
	TileLayer *l = data;

	g_free (l->name);
	g_free (l->tiles);
	g_free (l);
}

static TileLayer *
tile_layer_new (const gchar *name,
                gsize        n_cells)
{
	TileLayer *l = g_new0 (TileLayer, 1);

	l->name = g_strdup (name);
	l->visible = TRUE;
	l->tiles = g_new0 (guint, n_cells);
	return l;
}

static TileLayer *
get_layer (LrgNodeTilemap *self,
           guint           layer)
{
	if (layer >= self->layers->len)
		return NULL;
	return g_ptr_array_index (self->layers, layer);
}

static void
lrg_node_tilemap_finalize (GObject *object)
{
	LrgNodeTilemap *self = LRG_NODE_TILEMAP (object);

	g_clear_pointer (&self->layers, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_node_tilemap_parent_class)->finalize (object);
}

static void
lrg_node_tilemap_class_init (LrgNodeTilemapClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_node_tilemap_finalize;
}

static void
lrg_node_tilemap_init (LrgNodeTilemap *self)
{
	self->layers = g_ptr_array_new_with_free_func (tile_layer_free);
	self->tile_width = 16;
	self->stagger_axis = LRG_TILEMAP_STAGGER_AXIS_Y;
	self->tile_height = 16;
}

LrgNodeTilemap *
lrg_node_tilemap_new (guint width,
                      guint height)
{
	LrgNodeTilemap *self;

	g_return_val_if_fail (width > 0 && width <= LRG_NODE_TILEMAP_MAX_SIZE, NULL);
	g_return_val_if_fail (height > 0 && height <= LRG_NODE_TILEMAP_MAX_SIZE, NULL);

	self = g_object_new (LRG_TYPE_NODE_TILEMAP, NULL);
	self->width = width;
	self->height = height;
	lrg_node_tilemap_add_layer (self, "base");
	return self;
}

guint
lrg_node_tilemap_get_width (LrgNodeTilemap *self)
{
	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), 0);
	return self->width;
}

guint
lrg_node_tilemap_get_height (LrgNodeTilemap *self)
{
	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), 0);
	return self->height;
}

gboolean
lrg_node_tilemap_resize (LrgNodeTilemap *self,
                         guint           width,
                         guint           height)
{
	guint i;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), FALSE);

	if (width == 0 || height == 0 || width > LRG_NODE_TILEMAP_MAX_SIZE || height > LRG_NODE_TILEMAP_MAX_SIZE)
		return FALSE;
	if (width == self->width && height == self->height)
		return TRUE;

	/* Copy the overlapping top-left block of every layer. */
	for (i = 0; i < self->layers->len; i++)
	{
		TileLayer *l = g_ptr_array_index (self->layers, i);
		guint     *tiles = g_new0 (guint, (gsize) width * height);
		guint      y, rows = MIN (height, self->height), cols = MIN (width, self->width);

		for (y = 0; y < rows; y++)
			memcpy (tiles + (gsize) y * width, l->tiles + (gsize) y * self->width, cols * sizeof (guint));
		g_free (l->tiles);
		l->tiles = tiles;
	}
	self->width = width;
	self->height = height;
	return TRUE;
}

guint
lrg_node_tilemap_get_tile_width (LrgNodeTilemap *self)
{
	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), 0);
	return self->tile_width;
}

guint
lrg_node_tilemap_get_tile_height (LrgNodeTilemap *self)
{
	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), 0);
	return self->tile_height;
}

void
lrg_node_tilemap_set_tile_size (LrgNodeTilemap *self,
                                guint           tile_width,
                                guint           tile_height)
{
	g_return_if_fail (LRG_IS_NODE_TILEMAP (self));
	g_return_if_fail (tile_width > 0 && tile_height > 0);

	self->tile_width = tile_width;
	self->tile_height = tile_height;
}

guint
lrg_node_tilemap_get_n_layers (LrgNodeTilemap *self)
{
	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), 0);
	return self->layers->len;
}

guint
lrg_node_tilemap_add_layer (LrgNodeTilemap *self,
                            const gchar    *name)
{
	g_autofree gchar *fallback = NULL;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), 0);

	if (name == NULL)
		name = fallback = g_strdup_printf ("layer %u", self->layers->len + 1);
	g_ptr_array_add (self->layers, tile_layer_new (name, (gsize) self->width * self->height));
	return self->layers->len - 1;
}

gboolean
lrg_node_tilemap_remove_layer (LrgNodeTilemap *self,
                               guint           layer)
{
	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), FALSE);

	if (layer >= self->layers->len || self->layers->len <= 1)
		return FALSE;
	g_ptr_array_remove_index (self->layers, layer);
	return TRUE;
}

gint
lrg_node_tilemap_find_layer (LrgNodeTilemap *self,
                             const gchar    *name)
{
	guint i;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), -1);

	for (i = 0; name != NULL && i < self->layers->len; i++)
		if (g_strcmp0 (((TileLayer *) g_ptr_array_index (self->layers, i))->name, name) == 0)
			return (gint) i;
	return -1;
}

const gchar *
lrg_node_tilemap_get_layer_name (LrgNodeTilemap *self,
                                 guint           layer)
{
	TileLayer *l;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), NULL);
	l = get_layer (self, layer);
	return (l != NULL) ? l->name : NULL;
}

void
lrg_node_tilemap_set_layer_name (LrgNodeTilemap *self,
                                 guint           layer,
                                 const gchar    *name)
{
	TileLayer *l;

	g_return_if_fail (LRG_IS_NODE_TILEMAP (self));
	g_return_if_fail (name != NULL);

	if ((l = get_layer (self, layer)) == NULL)
		return;
	g_free (l->name);
	l->name = g_strdup (name);
}

gboolean
lrg_node_tilemap_get_layer_visible (LrgNodeTilemap *self,
                                    guint           layer)
{
	TileLayer *l;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), FALSE);
	l = get_layer (self, layer);
	return (l != NULL) && l->visible;
}

void
lrg_node_tilemap_set_layer_visible (LrgNodeTilemap *self,
                                    guint           layer,
                                    gboolean        visible)
{
	TileLayer *l;

	g_return_if_fail (LRG_IS_NODE_TILEMAP (self));
	if ((l = get_layer (self, layer)) != NULL)
		l->visible = visible != FALSE;
}

gboolean
lrg_node_tilemap_in_bounds (LrgNodeTilemap *self,
                            gint            x,
                            gint            y)
{
	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), FALSE);
	return x >= 0 && y >= 0 && (guint) x < self->width && (guint) y < self->height;
}

guint
lrg_node_tilemap_get_tile (LrgNodeTilemap *self,
                           guint           layer,
                           gint            x,
                           gint            y)
{
	TileLayer *l;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), 0);

	l = get_layer (self, layer);
	if (l == NULL || !lrg_node_tilemap_in_bounds (self, x, y))
		return 0;
	return l->tiles[(gsize) y * self->width + (guint) x];
}

gboolean
lrg_node_tilemap_set_tile (LrgNodeTilemap *self,
                           guint           layer,
                           gint            x,
                           gint            y,
                           guint           tile)
{
	TileLayer *l;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), FALSE);

	l = get_layer (self, layer);
	if (l == NULL || !lrg_node_tilemap_in_bounds (self, x, y))
		return FALSE;
	l->tiles[(gsize) y * self->width + (guint) x] = tile;
	return TRUE;
}

const guint *
lrg_node_tilemap_get_layer_tiles (LrgNodeTilemap *self,
                                  guint           layer,
                                  gsize          *n_tiles)
{
	TileLayer *l;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), NULL);
	g_return_val_if_fail (n_tiles != NULL, NULL);

	l = get_layer (self, layer);
	*n_tiles = (l != NULL) ? (gsize) self->width * self->height : 0;
	return (l != NULL) ? l->tiles : NULL;
}

gboolean
lrg_node_tilemap_set_layer_tiles (LrgNodeTilemap *self,
                                  guint           layer,
                                  const guint    *tiles,
                                  gsize           n_tiles)
{
	TileLayer *l;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), FALSE);

	l = get_layer (self, layer);
	if (l == NULL || tiles == NULL || n_tiles != (gsize) self->width * self->height)
		return FALSE;
	memcpy (l->tiles, tiles, n_tiles * sizeof (guint));
	return TRUE;
}

/*
 * Breadth-first flood over 4-neighbours with the seed's id. A visited
 * bitmap keeps it O(cells); the output array doubles as the BFS queue.
 */
GArray *
lrg_node_tilemap_flood_region (LrgNodeTilemap *self,
                               guint           layer,
                               gint            x,
                               gint            y)
{
	GArray           *out;
	TileLayer        *l;
	g_autofree guint8 *seen = NULL;
	guint             target;
	guint             head = 0;
	guint             seed;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), NULL);

	out = g_array_new (FALSE, FALSE, sizeof (guint));
	l = get_layer (self, layer);
	if (l == NULL || !lrg_node_tilemap_in_bounds (self, x, y))
		return out;

	seen = g_new0 (guint8, (gsize) self->width * self->height);
	seed = (guint) y * self->width + (guint) x;
	target = l->tiles[seed];
	seen[seed] = 1;
	g_array_append_val (out, seed);

	while (head < out->len)
	{
		guint idx = g_array_index (out, guint, head++);
		guint cx = idx % self->width;
		guint cy = idx / self->width;
		guint n[4];
		guint nn = 0, k;

		if (cx > 0) n[nn++] = idx - 1;
		if (cx + 1 < self->width) n[nn++] = idx + 1;
		if (cy > 0) n[nn++] = idx - self->width;
		if (cy + 1 < self->height) n[nn++] = idx + self->width;

		for (k = 0; k < nn; k++)
		{
			if (seen[n[k]] || l->tiles[n[k]] != target)
				continue;
			seen[n[k]] = 1;
			g_array_append_val (out, n[k]);
		}
	}
	return out;
}

guint
lrg_node_tilemap_count_tiles (LrgNodeTilemap *self,
                              guint           layer)
{
	TileLayer *l;
	gsize      i, n;
	guint      count = 0;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), 0);

	if ((l = get_layer (self, layer)) == NULL)
		return 0;
	n = (gsize) self->width * self->height;
	for (i = 0; i < n; i++)
		if (l->tiles[i] != 0)
			count++;
	return count;
}

gboolean
lrg_node_tilemap_local_to_cell (LrgNodeTilemap *self,
                                gfloat          tile_size,
                                gfloat          local_x,
                                gfloat          local_y,
                                gint           *out_x,
                                gint           *out_y)
{
	gint cx, cy;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), FALSE);
	g_return_val_if_fail (out_x != NULL && out_y != NULL, FALSE);

	if (!(tile_size > 0.0f))
		tile_size = 1.0f;

	/* Rows grow downwards (-y) from the node origin. */
	cx = (gint) floorf (local_x / tile_size);
	cy = (gint) floorf (-local_y / tile_size);
	*out_x = cx;
	*out_y = cy;
	return lrg_node_tilemap_in_bounds (self, cx, cy);
}

LrgNodeTilemap *
lrg_node_tilemap_copy (LrgNodeTilemap *self)
{
	LrgNodeTilemap *copy;
	guint           i;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), NULL);

	copy = g_object_new (LRG_TYPE_NODE_TILEMAP, NULL);
	copy->width = self->width;
	copy->height = self->height;
	copy->tile_width = self->tile_width;
	copy->tile_height = self->tile_height;
	copy->orientation = self->orientation;
	copy->stagger_axis = self->stagger_axis;
	copy->stagger_index = self->stagger_index;
	copy->hex_side_length = self->hex_side_length;
	copy->cell_width = self->cell_width;
	copy->cell_height = self->cell_height;
	for (i = 0; i < self->layers->len; i++)
	{
		TileLayer *src = g_ptr_array_index (self->layers, i);
		TileLayer *dst = tile_layer_new (src->name, (gsize) self->width * self->height);

		dst->visible = src->visible;
		memcpy (dst->tiles, src->tiles, (gsize) self->width * self->height * sizeof (guint));
		g_ptr_array_add (copy->layers, dst);
	}
	return copy;
}

gboolean
lrg_node_tilemap_equal (LrgNodeTilemap *a,
                        LrgNodeTilemap *b)
{
	guint i;

	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL)
		return FALSE;
	if (a->width != b->width || a->height != b->height ||
	    a->tile_width != b->tile_width || a->tile_height != b->tile_height ||
	    a->orientation != b->orientation || a->stagger_axis != b->stagger_axis ||
	    a->stagger_index != b->stagger_index || a->hex_side_length != b->hex_side_length ||
	    a->cell_width != b->cell_width || a->cell_height != b->cell_height ||
	    a->layers->len != b->layers->len)
		return FALSE;

	for (i = 0; i < a->layers->len; i++)
	{
		TileLayer *la = g_ptr_array_index (a->layers, i);
		TileLayer *lb = g_ptr_array_index (b->layers, i);

		if (g_strcmp0 (la->name, lb->name) != 0 || la->visible != lb->visible ||
		    memcmp (la->tiles, lb->tiles, (gsize) a->width * a->height * sizeof (guint)) != 0)
			return FALSE;
	}
	return TRUE;
}

LrgTilemap *
lrg_node_tilemap_to_runtime (LrgNodeTilemap *self,
                             LrgTileset     *tileset)
{
	LrgTilemap *map;
	guint       i;

	g_return_val_if_fail (LRG_IS_NODE_TILEMAP (self), NULL);
	g_return_val_if_fail (LRG_IS_TILESET (tileset), NULL);

	map = lrg_tilemap_new (tileset);
	lrg_tilemap_set_orientation (map, self->orientation);
	lrg_tilemap_set_stagger_axis (map, self->stagger_axis);
	lrg_tilemap_set_stagger_index (map, self->stagger_index);
	lrg_tilemap_set_hex_side_length (map, self->hex_side_length);
	lrg_tilemap_set_cell_size (map, self->cell_width, self->cell_height);
	for (i = 0; i < self->layers->len; i++)
	{
		TileLayer                *src = g_ptr_array_index (self->layers, i);
		g_autoptr(LrgTilemapLayer) layer = lrg_tilemap_layer_new (self->width, self->height);

		lrg_tilemap_layer_set_name (layer, src->name);
		lrg_tilemap_layer_set_visible (layer, src->visible);
		lrg_tilemap_layer_set_tiles (layer, src->tiles, (gsize) self->width * self->height);
		lrg_tilemap_add_layer (map, layer);
	}
	return map;
}

void
lrg_node_tilemap_set_grid (LrgNodeTilemap         *self,
                           LrgTilemapOrientation   orientation,
                           LrgTilemapStaggerAxis   stagger_axis,
                           LrgTilemapStaggerIndex  stagger_index,
                           guint                   hex_side_length,
                           guint                   cell_width,
                           guint                   cell_height)
{
	g_return_if_fail (LRG_IS_NODE_TILEMAP (self));
	g_return_if_fail (orientation <= LRG_TILEMAP_ORIENTATION_HEXAGONAL);

	self->orientation = orientation;
	self->stagger_axis = stagger_axis;
	self->stagger_index = stagger_index;
	self->hex_side_length = hex_side_length;
	self->cell_width = cell_width;
	self->cell_height = cell_height;
}

void
lrg_node_tilemap_get_grid (LrgNodeTilemap         *self,
                           LrgTilemapOrientation  *orientation,
                           LrgTilemapStaggerAxis  *stagger_axis,
                           LrgTilemapStaggerIndex *stagger_index,
                           guint                  *hex_side_length,
                           guint                  *cell_width,
                           guint                  *cell_height)
{
	g_return_if_fail (LRG_IS_NODE_TILEMAP (self));

	if (orientation != NULL)
		*orientation = self->orientation;
	if (stagger_axis != NULL)
		*stagger_axis = self->stagger_axis;
	if (stagger_index != NULL)
		*stagger_index = self->stagger_index;
	if (hex_side_length != NULL)
		*hex_side_length = self->hex_side_length;
	if (cell_width != NULL)
		*cell_width = self->cell_width;
	if (cell_height != NULL)
		*cell_height = self->cell_height;
}
