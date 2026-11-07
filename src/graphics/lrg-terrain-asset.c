/* lrg-terrain-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainAsset: (stub, being written)
 */

#include "config.h"

#include "lrg-terrain-asset.h"

struct _LrgTerrainAsset
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgTerrainAsset, lrg_terrain_asset, G_TYPE_OBJECT)

static void
lrg_terrain_asset_class_init (LrgTerrainAssetClass *klass)
{
}

static void
lrg_terrain_asset_init (LrgTerrainAsset *self)
{
}

LrgTerrainAsset *
lrg_terrain_asset_new (void)
{
	return g_object_new (LRG_TYPE_TERRAIN_ASSET, NULL);
}
