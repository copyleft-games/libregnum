/* lrg-expansion-pack.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Expansion pack DLC type.
 *
 * Expansion packs are major content additions that typically include
 * new campaigns, areas, level cap increases, and significant gameplay.
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

#define LRG_TYPE_EXPANSION_PACK (lrg_expansion_pack_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgExpansionPack, lrg_expansion_pack, LRG, EXPANSION_PACK, LrgDlc)

/**
 * lrg_expansion_pack_new:
 * @manifest: the mod manifest
 * @base_path: the DLC's base directory path
 *
 * Creates a new expansion pack DLC.
 *
 * Returns: (transfer full): a new #LrgExpansionPack
 */
LRG_AVAILABLE_IN_ALL
LrgExpansionPack * lrg_expansion_pack_new (LrgModManifest *manifest,
                                            const gchar    *base_path);

/**
 * lrg_expansion_pack_get_campaign_name:
 * @self: a #LrgExpansionPack
 *
 * Gets the main campaign/storyline name.
 *
 * Returns: (transfer none) (nullable): the campaign name
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_expansion_pack_get_campaign_name (LrgExpansionPack *self);

/**
 * lrg_expansion_pack_set_campaign_name:
 * @self: a #LrgExpansionPack
 * @name: the campaign name
 *
 * Sets the main campaign/storyline name.
 */
LRG_AVAILABLE_IN_ALL
void lrg_expansion_pack_set_campaign_name (LrgExpansionPack *self,
                                            const gchar      *name);

/**
 * lrg_expansion_pack_get_level_cap_increase:
 * @self: a #LrgExpansionPack
 *
 * Gets the level cap increase amount.
 *
 * Returns: the level cap increase
 */
LRG_AVAILABLE_IN_ALL
guint lrg_expansion_pack_get_level_cap_increase (LrgExpansionPack *self);

/**
 * lrg_expansion_pack_set_level_cap_increase:
 * @self: a #LrgExpansionPack
 * @increase: the level cap increase
 *
 * Sets the level cap increase amount.
 */
LRG_AVAILABLE_IN_ALL
void lrg_expansion_pack_set_level_cap_increase (LrgExpansionPack *self,
                                                 guint             increase);

/**
 * lrg_expansion_pack_get_new_areas:
 * @self: a #LrgExpansionPack
 *
 * Gets the list of new area IDs.
 *
 * Returns: (transfer none) (element-type utf8): array of area IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_expansion_pack_get_new_areas (LrgExpansionPack *self);

/**
 * lrg_expansion_pack_add_new_area:
 * @self: a #LrgExpansionPack
 * @area_id: the area identifier
 *
 * Adds a new area ID.
 */
LRG_AVAILABLE_IN_ALL
void lrg_expansion_pack_add_new_area (LrgExpansionPack *self,
                                       const gchar      *area_id);

G_END_DECLS
