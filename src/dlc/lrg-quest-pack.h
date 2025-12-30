/* lrg-quest-pack.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Quest pack DLC type.
 *
 * Quest packs contain additional quests, missions, and storylines.
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

#define LRG_TYPE_QUEST_PACK (lrg_quest_pack_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgQuestPack, lrg_quest_pack, LRG, QUEST_PACK, LrgDlc)

/**
 * lrg_quest_pack_new:
 * @manifest: the mod manifest
 * @base_path: the DLC's base directory path
 *
 * Creates a new quest pack DLC.
 *
 * Returns: (transfer full): a new #LrgQuestPack
 */
LRG_AVAILABLE_IN_ALL
LrgQuestPack * lrg_quest_pack_new (LrgModManifest *manifest,
                                    const gchar    *base_path);

/**
 * lrg_quest_pack_get_quest_ids:
 * @self: a #LrgQuestPack
 *
 * Gets the list of quest IDs.
 *
 * Returns: (transfer none) (element-type utf8): array of quest IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_quest_pack_get_quest_ids (LrgQuestPack *self);

/**
 * lrg_quest_pack_add_quest_id:
 * @self: a #LrgQuestPack
 * @quest_id: the quest identifier
 *
 * Adds a quest ID.
 */
LRG_AVAILABLE_IN_ALL
void lrg_quest_pack_add_quest_id (LrgQuestPack *self,
                                   const gchar  *quest_id);

/**
 * lrg_quest_pack_get_estimated_hours:
 * @self: a #LrgQuestPack
 *
 * Gets the estimated playtime in hours.
 *
 * Returns: the estimated hours
 */
LRG_AVAILABLE_IN_ALL
guint lrg_quest_pack_get_estimated_hours (LrgQuestPack *self);

/**
 * lrg_quest_pack_set_estimated_hours:
 * @self: a #LrgQuestPack
 * @hours: the estimated playtime
 *
 * Sets the estimated playtime in hours.
 */
LRG_AVAILABLE_IN_ALL
void lrg_quest_pack_set_estimated_hours (LrgQuestPack *self,
                                          guint         hours);

G_END_DECLS
