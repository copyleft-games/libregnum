/* lrg-procgen-tile-rules.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tag -> tile mapping with autotiling.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PROCGEN

#include "lrg-procgen-tile-rules.h"
#include "../lrg-log.h"

#include <string.h>

/* 8-bit neighbour mask bits (blob) */
#define BLOB_N  (1u << 0)
#define BLOB_NE (1u << 1)
#define BLOB_E  (1u << 2)
#define BLOB_SE (1u << 3)
#define BLOB_S  (1u << 4)
#define BLOB_SW (1u << 5)
#define BLOB_W  (1u << 6)
#define BLOB_NW (1u << 7)

struct _LrgProcgenTileRules
{
	GObject             parent_instance;

	guint               default_tile;
	gboolean            edge_connects;
	gboolean            has_rule[256];
	guint               tile[256];
	LrgProcgenAutotile  autotile[256];
	guint8              connects[256][32]; /* bitset: tag -> other tags */
	GHashTable         *variants;          /* (tag << 16 | mask) -> tile */
};

G_DEFINE_FINAL_TYPE (LrgProcgenTileRules, lrg_procgen_tile_rules, G_TYPE_OBJECT)

static void
lrg_procgen_tile_rules_finalize (GObject *object)
{
	LrgProcgenTileRules *self = LRG_PROCGEN_TILE_RULES (object);

	g_clear_pointer (&self->variants, g_hash_table_unref);
	G_OBJECT_CLASS (lrg_procgen_tile_rules_parent_class)->finalize (object);
}

static void
lrg_procgen_tile_rules_class_init (LrgProcgenTileRulesClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_procgen_tile_rules_finalize;
}

static void
lrg_procgen_tile_rules_init (LrgProcgenTileRules *self)
{
	self->default_tile = LRG_TILEMAP_EMPTY_TILE;
	self->edge_connects = TRUE;
	self->variants = g_hash_table_new (g_direct_hash, g_direct_equal);
}

LrgProcgenTileRules *
lrg_procgen_tile_rules_new (void)
{
	return g_object_new (LRG_TYPE_PROCGEN_TILE_RULES, NULL);
}

void
lrg_procgen_tile_rules_set_default_tile (LrgProcgenTileRules *self,
                                         guint                tile_id)
{
	g_return_if_fail (LRG_IS_PROCGEN_TILE_RULES (self));
	self->default_tile = tile_id;
}

guint
lrg_procgen_tile_rules_get_default_tile (LrgProcgenTileRules *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_TILE_RULES (self), 0);
	return self->default_tile;
}

void
lrg_procgen_tile_rules_set_rule (LrgProcgenTileRules *self,
                                 guint8               tag,
                                 guint                tile_id,
                                 LrgProcgenAutotile   autotile)
{
	g_return_if_fail (LRG_IS_PROCGEN_TILE_RULES (self));
	g_return_if_fail (autotile <= LRG_PROCGEN_AUTOTILE_BLOB);
	self->has_rule[tag] = TRUE;
	self->tile[tag] = tile_id;
	self->autotile[tag] = autotile;
}

/* drops every variant override of @tag */
static gboolean
variant_has_tag (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
	return (GPOINTER_TO_UINT (key) >> 16) == GPOINTER_TO_UINT (user_data);
}

void
lrg_procgen_tile_rules_remove_rule (LrgProcgenTileRules *self,
                                    guint8               tag)
{
	g_return_if_fail (LRG_IS_PROCGEN_TILE_RULES (self));
	self->has_rule[tag] = FALSE;
	self->tile[tag] = 0;
	self->autotile[tag] = LRG_PROCGEN_AUTOTILE_NONE;
	memset (self->connects[tag], 0, sizeof self->connects[tag]);
	g_hash_table_foreach_remove (self->variants, variant_has_tag, GUINT_TO_POINTER ((guint) tag));
}

gboolean
lrg_procgen_tile_rules_get_rule (LrgProcgenTileRules *self,
                                 guint8               tag,
                                 guint               *out_tile_id,
                                 LrgProcgenAutotile  *out_autotile)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_TILE_RULES (self), FALSE);
	if (out_tile_id != NULL)
		*out_tile_id = self->tile[tag];
	if (out_autotile != NULL)
		*out_autotile = self->autotile[tag];
	return self->has_rule[tag];
}

void
lrg_procgen_tile_rules_set_variant (LrgProcgenTileRules *self,
                                    guint8               tag,
                                    guint                mask,
                                    guint                tile_id)
{
	g_return_if_fail (LRG_IS_PROCGEN_TILE_RULES (self));
	g_return_if_fail (mask <= 0xff);
	g_hash_table_insert (self->variants,
	                     GUINT_TO_POINTER (((guint) tag << 16) | mask),
	                     GUINT_TO_POINTER (tile_id));
}

gboolean
lrg_procgen_tile_rules_get_variant (LrgProcgenTileRules *self,
                                    guint8               tag,
                                    guint                mask,
                                    guint               *out_tile_id)
{
	gpointer tile;

	g_return_val_if_fail (LRG_IS_PROCGEN_TILE_RULES (self), FALSE);
	if (mask > 0xff ||
	    !g_hash_table_lookup_extended (self->variants,
	                                   GUINT_TO_POINTER (((guint) tag << 16) | mask),
	                                   NULL, &tile))
		return FALSE;
	if (out_tile_id != NULL)
		*out_tile_id = GPOINTER_TO_UINT (tile);
	return TRUE;
}

gboolean
lrg_procgen_tile_rules_get_connects (LrgProcgenTileRules *self,
                                     guint8               tag,
                                     guint8               other)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_TILE_RULES (self), FALSE);
	return (self->connects[tag][other >> 3] >> (other & 7)) & 1;
}

void
lrg_procgen_tile_rules_set_connects (LrgProcgenTileRules *self,
                                     guint8               tag,
                                     guint8               other,
                                     gboolean             connects)
{
	g_return_if_fail (LRG_IS_PROCGEN_TILE_RULES (self));
	if (connects)
		self->connects[tag][other >> 3] |= (guint8) (1u << (other & 7));
	else
		self->connects[tag][other >> 3] &= (guint8) ~(1u << (other & 7));
}

void
lrg_procgen_tile_rules_set_edge_connects (LrgProcgenTileRules *self,
                                          gboolean             connects)
{
	g_return_if_fail (LRG_IS_PROCGEN_TILE_RULES (self));
	self->edge_connects = connects ? TRUE : FALSE;
}

gboolean
lrg_procgen_tile_rules_get_edge_connects (LrgProcgenTileRules *self)
{
	g_return_val_if_fail (LRG_IS_PROCGEN_TILE_RULES (self), FALSE);
	return self->edge_connects;
}

/* whether the neighbour (x, y) matches a cell tagged @tag */
static gboolean
matches (LrgProcgenTileRules *self,
         LrgProcgenGrid      *grid,
         guint8               tag,
         gint                 x,
         gint                 y)
{
	guint8 other;

	if (!lrg_procgen_grid_in_bounds (grid, x, y))
		return self->edge_connects;
	other = lrg_procgen_grid_get_cell (grid, x, y);
	if (other == tag)
		return TRUE;
	return (self->connects[tag][other >> 3] >> (other & 7)) & 1;
}

/* drops the corner bits whose two edges are not both set */
static guint
blob_reduce (guint mask)
{
	if ((mask & (BLOB_N | BLOB_E)) != (BLOB_N | BLOB_E))
		mask &= ~BLOB_NE;
	if ((mask & (BLOB_E | BLOB_S)) != (BLOB_E | BLOB_S))
		mask &= ~BLOB_SE;
	if ((mask & (BLOB_S | BLOB_W)) != (BLOB_S | BLOB_W))
		mask &= ~BLOB_SW;
	if ((mask & (BLOB_W | BLOB_N)) != (BLOB_W | BLOB_N))
		mask &= ~BLOB_NW;
	return mask & 0xff;
}

guint
lrg_procgen_tile_rules_blob_index (guint mask)
{
	static gsize init = 0;
	static guint8 index_of[256];
	guint reduced;

	/* number the 47 masks that are their own reduction, ascending */
	if (g_once_init_enter (&init))
	{
		guint m;
		guint next = 0;

		for (m = 0; m < 256; m++)
			index_of[m] = (blob_reduce (m) == m) ? (guint8) next++ : 0;
		g_assert (next == LRG_PROCGEN_BLOB_VARIANTS);
		g_once_init_leave (&init, 1);
	}
	reduced = blob_reduce (mask & 0xff);
	return index_of[reduced];
}

guint
lrg_procgen_tile_rules_neighbour_mask (LrgProcgenTileRules *self,
                                       LrgProcgenGrid      *grid,
                                       gint                 x,
                                       gint                 y,
                                       LrgProcgenAutotile   autotile)
{
	guint8 tag;
	guint mask = 0;

	g_return_val_if_fail (LRG_IS_PROCGEN_TILE_RULES (self), 0);
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (grid), 0);

	tag = lrg_procgen_grid_get_cell (grid, x, y);
	switch (autotile)
	{
	case LRG_PROCGEN_AUTOTILE_CARDINAL:
		mask |= matches (self, grid, tag, x, y - 1) ? 1u : 0u;
		mask |= matches (self, grid, tag, x + 1, y) ? 2u : 0u;
		mask |= matches (self, grid, tag, x, y + 1) ? 4u : 0u;
		mask |= matches (self, grid, tag, x - 1, y) ? 8u : 0u;
		return mask;
	case LRG_PROCGEN_AUTOTILE_BLOB:
		mask |= matches (self, grid, tag, x, y - 1) ? BLOB_N : 0u;
		mask |= matches (self, grid, tag, x + 1, y - 1) ? BLOB_NE : 0u;
		mask |= matches (self, grid, tag, x + 1, y) ? BLOB_E : 0u;
		mask |= matches (self, grid, tag, x + 1, y + 1) ? BLOB_SE : 0u;
		mask |= matches (self, grid, tag, x, y + 1) ? BLOB_S : 0u;
		mask |= matches (self, grid, tag, x - 1, y + 1) ? BLOB_SW : 0u;
		mask |= matches (self, grid, tag, x - 1, y) ? BLOB_W : 0u;
		mask |= matches (self, grid, tag, x - 1, y - 1) ? BLOB_NW : 0u;
		return blob_reduce (mask);
	case LRG_PROCGEN_AUTOTILE_NONE:
	default:
		return 0;
	}
}

guint
lrg_procgen_tile_rules_resolve (LrgProcgenTileRules *self,
                                LrgProcgenGrid      *grid,
                                gint                 x,
                                gint                 y)
{
	guint8 tag;
	guint mask;
	gpointer override;

	g_return_val_if_fail (LRG_IS_PROCGEN_TILE_RULES (self), 0);
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (grid), 0);

	tag = lrg_procgen_grid_get_cell (grid, x, y);
	if (!self->has_rule[tag])
		return self->default_tile;
	if (self->autotile[tag] == LRG_PROCGEN_AUTOTILE_NONE)
		return self->tile[tag];

	/* an explicit variant wins over the computed offset */
	mask = lrg_procgen_tile_rules_neighbour_mask (self, grid, x, y, self->autotile[tag]);
	if (g_hash_table_lookup_extended (self->variants,
	                                  GUINT_TO_POINTER (((guint) tag << 16) | mask),
	                                  NULL, &override))
		return GPOINTER_TO_UINT (override);
	if (self->autotile[tag] == LRG_PROCGEN_AUTOTILE_BLOB)
		return self->tile[tag] + lrg_procgen_tile_rules_blob_index (mask);
	return self->tile[tag] + mask;
}

LrgTilemapLayer *
lrg_procgen_tile_rules_build_layer (LrgProcgenTileRules *self,
                                    LrgProcgenGrid      *grid)
{
	LrgTilemapLayer *layer;
	guint w;
	guint h;
	guint x;
	guint y;

	g_return_val_if_fail (LRG_IS_PROCGEN_TILE_RULES (self), NULL);
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (grid), NULL);

	w = lrg_procgen_grid_get_width (grid);
	h = lrg_procgen_grid_get_height (grid);
	layer = lrg_tilemap_layer_new (w, h);
	lrg_tilemap_layer_set_name (layer, "procgen");
	lrg_tilemap_layer_set_collision_enabled (layer, TRUE);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
			lrg_tilemap_layer_set_tile (layer, x, y,
			                            lrg_procgen_tile_rules_resolve (self, grid, (gint) x, (gint) y));
	return layer;
}

LrgTilemap *
lrg_procgen_tile_rules_build_tilemap (LrgProcgenTileRules *self,
                                      LrgProcgenGrid      *grid,
                                      LrgTileset          *tileset)
{
	g_autoptr(LrgTilemapLayer) layer = NULL;
	LrgTilemap *map;
	guint w;
	guint h;
	guint x;
	guint y;

	g_return_val_if_fail (LRG_IS_PROCGEN_TILE_RULES (self), NULL);
	g_return_val_if_fail (LRG_IS_PROCGEN_GRID (grid), NULL);
	g_return_val_if_fail (LRG_IS_TILESET (tileset), NULL);

	layer = lrg_procgen_tile_rules_build_layer (self, grid);
	map = lrg_tilemap_new (tileset);
	lrg_tilemap_add_layer (map, layer);

	/* tiles that non-walkable cells use become solid in the tileset */
	w = lrg_procgen_grid_get_width (grid);
	h = lrg_procgen_grid_get_height (grid);
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++)
		{
			guint tile = lrg_tilemap_layer_get_tile (layer, x, y);

			if (tile == LRG_TILEMAP_EMPTY_TILE ||
			    lrg_procgen_grid_is_walkable (grid, (gint) x, (gint) y))
				continue;
			if (tile - 1 < lrg_tileset_get_tile_count (tileset))
				lrg_tileset_set_tile_properties (tileset, tile - 1,
				                                 lrg_tileset_get_tile_properties (tileset, tile - 1) |
				                                 LRG_TILE_PROPERTY_SOLID);
		}
	return map;
}
