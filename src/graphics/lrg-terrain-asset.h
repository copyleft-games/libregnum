/* lrg-terrain-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainAsset: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_TERRAIN_ASSET (lrg_terrain_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTerrainAsset, lrg_terrain_asset, LRG, TERRAIN_ASSET, GObject)

/**
 * lrg_terrain_asset_new:
 *
 * Returns: (transfer full): a new #LrgTerrainAsset
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainAsset * lrg_terrain_asset_new (void);

G_END_DECLS
