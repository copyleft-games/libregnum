/* lrg-item-pack.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Item pack DLC type.
 *
 * Item packs contain additional items, equipment, and gear.
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

#define LRG_TYPE_ITEM_PACK (lrg_item_pack_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgItemPack, lrg_item_pack, LRG, ITEM_PACK, LrgDlc)

/**
 * lrg_item_pack_new:
 * @manifest: the mod manifest
 * @base_path: the DLC's base directory path
 *
 * Creates a new item pack DLC.
 *
 * Returns: (transfer full): a new #LrgItemPack
 */
LRG_AVAILABLE_IN_ALL
LrgItemPack * lrg_item_pack_new (LrgModManifest *manifest,
                                  const gchar    *base_path);

/**
 * lrg_item_pack_get_item_ids:
 * @self: a #LrgItemPack
 *
 * Gets the list of item definition IDs.
 *
 * Returns: (transfer none) (element-type utf8): array of item IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_item_pack_get_item_ids (LrgItemPack *self);

/**
 * lrg_item_pack_add_item_id:
 * @self: a #LrgItemPack
 * @item_id: the item identifier
 *
 * Adds an item ID.
 */
LRG_AVAILABLE_IN_ALL
void lrg_item_pack_add_item_id (LrgItemPack *self,
                                 const gchar *item_id);

/**
 * lrg_item_pack_get_equipment_slots:
 * @self: a #LrgItemPack
 *
 * Gets the list of affected equipment slot types.
 *
 * Returns: (transfer none) (element-type utf8): array of slot types
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_item_pack_get_equipment_slots (LrgItemPack *self);

/**
 * lrg_item_pack_add_equipment_slot:
 * @self: a #LrgItemPack
 * @slot: the equipment slot type
 *
 * Adds an equipment slot type.
 */
LRG_AVAILABLE_IN_ALL
void lrg_item_pack_add_equipment_slot (LrgItemPack *self,
                                        const gchar *slot);

G_END_DECLS
