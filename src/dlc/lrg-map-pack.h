/* lrg-map-pack.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Map pack DLC type.
 *
 * Map packs contain additional maps, levels, or areas.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-dlc.h"

G_BEGIN_DECLS

#define LRG_TYPE_MAP_PACK (lrg_map_pack_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMapPack, lrg_map_pack, LRG, MAP_PACK, LrgDlc)

/**
 * lrg_map_pack_new:
 * @manifest: the mod manifest
 * @base_path: the DLC's base directory path
 *
 * Creates a new map pack DLC.
 *
 * Returns: (transfer full): a new #LrgMapPack
 */
LRG_AVAILABLE_IN_ALL
LrgMapPack * lrg_map_pack_new (LrgModManifest *manifest,
                                const gchar    *base_path);

/**
 * lrg_map_pack_get_map_ids:
 * @self: a #LrgMapPack
 *
 * Gets the list of map/level IDs.
 *
 * Returns: (transfer none) (element-type utf8): array of map IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_map_pack_get_map_ids (LrgMapPack *self);

/**
 * lrg_map_pack_add_map_id:
 * @self: a #LrgMapPack
 * @map_id: the map identifier
 *
 * Adds a map ID.
 */
LRG_AVAILABLE_IN_ALL
void lrg_map_pack_add_map_id (LrgMapPack  *self,
                               const gchar *map_id);

/**
 * lrg_map_pack_get_biome_type:
 * @self: a #LrgMapPack
 *
 * Gets the biome/theme type.
 *
 * Returns: (transfer none) (nullable): the biome type
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_map_pack_get_biome_type (LrgMapPack *self);

/**
 * lrg_map_pack_set_biome_type:
 * @self: a #LrgMapPack
 * @biome_type: the biome type
 *
 * Sets the biome/theme type.
 */
LRG_AVAILABLE_IN_ALL
void lrg_map_pack_set_biome_type (LrgMapPack  *self,
                                   const gchar *biome_type);

G_END_DECLS
