/* lrg-character-pack.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Character pack DLC type.
 *
 * Character packs contain additional playable characters or companions.
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

#define LRG_TYPE_CHARACTER_PACK (lrg_character_pack_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCharacterPack, lrg_character_pack, LRG, CHARACTER_PACK, LrgDlc)

/**
 * lrg_character_pack_new:
 * @manifest: the mod manifest
 * @base_path: the DLC's base directory path
 *
 * Creates a new character pack DLC.
 *
 * Returns: (transfer full): a new #LrgCharacterPack
 */
LRG_AVAILABLE_IN_ALL
LrgCharacterPack * lrg_character_pack_new (LrgModManifest *manifest,
                                            const gchar    *base_path);

/**
 * lrg_character_pack_get_character_ids:
 * @self: a #LrgCharacterPack
 *
 * Gets the list of character IDs.
 *
 * Returns: (transfer none) (element-type utf8): array of character IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_character_pack_get_character_ids (LrgCharacterPack *self);

/**
 * lrg_character_pack_add_character_id:
 * @self: a #LrgCharacterPack
 * @character_id: the character identifier
 *
 * Adds a character ID.
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_pack_add_character_id (LrgCharacterPack *self,
                                           const gchar      *character_id);

/**
 * lrg_character_pack_get_is_playable:
 * @self: a #LrgCharacterPack
 *
 * Gets whether the characters are playable.
 *
 * Returns: %TRUE if characters are playable
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_character_pack_get_is_playable (LrgCharacterPack *self);

/**
 * lrg_character_pack_set_is_playable:
 * @self: a #LrgCharacterPack
 * @is_playable: whether characters are playable
 *
 * Sets whether the characters are playable.
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_pack_set_is_playable (LrgCharacterPack *self,
                                          gboolean          is_playable);

/**
 * lrg_character_pack_get_is_companion:
 * @self: a #LrgCharacterPack
 *
 * Gets whether the characters are companions.
 *
 * Returns: %TRUE if characters are companions
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_character_pack_get_is_companion (LrgCharacterPack *self);

/**
 * lrg_character_pack_set_is_companion:
 * @self: a #LrgCharacterPack
 * @is_companion: whether characters are companions
 *
 * Sets whether the characters are companions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_character_pack_set_is_companion (LrgCharacterPack *self,
                                           gboolean          is_companion);

G_END_DECLS
