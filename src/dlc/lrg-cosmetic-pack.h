/* lrg-cosmetic-pack.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Cosmetic pack DLC type.
 *
 * Cosmetic packs contain visual customization items like skins,
 * effects, and other non-gameplay-affecting content.
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

#define LRG_TYPE_COSMETIC_PACK (lrg_cosmetic_pack_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCosmeticPack, lrg_cosmetic_pack, LRG, COSMETIC_PACK, LrgDlc)

/**
 * lrg_cosmetic_pack_new:
 * @manifest: the mod manifest
 * @base_path: the DLC's base directory path
 *
 * Creates a new cosmetic pack DLC.
 *
 * Returns: (transfer full): a new #LrgCosmeticPack
 */
LRG_AVAILABLE_IN_ALL
LrgCosmeticPack * lrg_cosmetic_pack_new (LrgModManifest *manifest,
                                          const gchar    *base_path);

/**
 * lrg_cosmetic_pack_get_skin_ids:
 * @self: a #LrgCosmeticPack
 *
 * Gets the list of skin IDs.
 *
 * Returns: (transfer none) (element-type utf8): array of skin IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_cosmetic_pack_get_skin_ids (LrgCosmeticPack *self);

/**
 * lrg_cosmetic_pack_add_skin_id:
 * @self: a #LrgCosmeticPack
 * @skin_id: the skin identifier
 *
 * Adds a skin ID.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cosmetic_pack_add_skin_id (LrgCosmeticPack *self,
                                     const gchar     *skin_id);

/**
 * lrg_cosmetic_pack_get_effect_ids:
 * @self: a #LrgCosmeticPack
 *
 * Gets the list of visual effect IDs.
 *
 * Returns: (transfer none) (element-type utf8): array of effect IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_cosmetic_pack_get_effect_ids (LrgCosmeticPack *self);

/**
 * lrg_cosmetic_pack_add_effect_id:
 * @self: a #LrgCosmeticPack
 * @effect_id: the effect identifier
 *
 * Adds an effect ID.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cosmetic_pack_add_effect_id (LrgCosmeticPack *self,
                                       const gchar     *effect_id);

/**
 * lrg_cosmetic_pack_get_preview_image:
 * @self: a #LrgCosmeticPack
 *
 * Gets the path to the preview image.
 *
 * Returns: (transfer none) (nullable): the preview image path
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_cosmetic_pack_get_preview_image (LrgCosmeticPack *self);

/**
 * lrg_cosmetic_pack_set_preview_image:
 * @self: a #LrgCosmeticPack
 * @path: the preview image path
 *
 * Sets the path to the preview image.
 */
LRG_AVAILABLE_IN_ALL
void lrg_cosmetic_pack_set_preview_image (LrgCosmeticPack *self,
                                           const gchar     *path);

G_END_DECLS
