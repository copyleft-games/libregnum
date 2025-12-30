/* lrg-workshop-item.h - Steam Workshop item wrapper
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_WORKSHOP_ITEM_H
#define LRG_WORKSHOP_ITEM_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_WORKSHOP_ITEM (lrg_workshop_item_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWorkshopItem, lrg_workshop_item, LRG, WORKSHOP_ITEM, GObject)

/**
 * LrgWorkshopItemState:
 * @LRG_WORKSHOP_ITEM_STATE_NONE: No state
 * @LRG_WORKSHOP_ITEM_STATE_SUBSCRIBED: Item is subscribed
 * @LRG_WORKSHOP_ITEM_STATE_LEGACY: Legacy item
 * @LRG_WORKSHOP_ITEM_STATE_INSTALLED: Item is installed
 * @LRG_WORKSHOP_ITEM_STATE_NEEDS_UPDATE: Item needs update
 * @LRG_WORKSHOP_ITEM_STATE_DOWNLOADING: Item is downloading
 * @LRG_WORKSHOP_ITEM_STATE_DOWNLOAD_PENDING: Download is pending
 *
 * State flags for Workshop items.
 */
typedef enum
{
    LRG_WORKSHOP_ITEM_STATE_NONE             = 0,
    LRG_WORKSHOP_ITEM_STATE_SUBSCRIBED       = 1 << 0,
    LRG_WORKSHOP_ITEM_STATE_LEGACY           = 1 << 1,
    LRG_WORKSHOP_ITEM_STATE_INSTALLED        = 1 << 2,
    LRG_WORKSHOP_ITEM_STATE_NEEDS_UPDATE     = 1 << 3,
    LRG_WORKSHOP_ITEM_STATE_DOWNLOADING      = 1 << 4,
    LRG_WORKSHOP_ITEM_STATE_DOWNLOAD_PENDING = 1 << 5
} LrgWorkshopItemState;

/**
 * LrgWorkshopItemVisibility:
 * @LRG_WORKSHOP_ITEM_VISIBILITY_PUBLIC: Visible to everyone
 * @LRG_WORKSHOP_ITEM_VISIBILITY_FRIENDS_ONLY: Visible to friends only
 * @LRG_WORKSHOP_ITEM_VISIBILITY_PRIVATE: Only visible to owner
 * @LRG_WORKSHOP_ITEM_VISIBILITY_UNLISTED: Unlisted (accessible via direct link)
 *
 * Visibility settings for Workshop items.
 */
typedef enum
{
    LRG_WORKSHOP_ITEM_VISIBILITY_PUBLIC       = 0,
    LRG_WORKSHOP_ITEM_VISIBILITY_FRIENDS_ONLY = 1,
    LRG_WORKSHOP_ITEM_VISIBILITY_PRIVATE      = 2,
    LRG_WORKSHOP_ITEM_VISIBILITY_UNLISTED     = 3
} LrgWorkshopItemVisibility;

/**
 * lrg_workshop_item_new:
 * @file_id: the Workshop file ID
 *
 * Creates a new Workshop item wrapper with the given file ID.
 * The item's details will be empty until populated from a query.
 *
 * Returns: (transfer full): a new #LrgWorkshopItem
 */
LRG_AVAILABLE_IN_ALL
LrgWorkshopItem *
lrg_workshop_item_new (guint64 file_id);

/**
 * lrg_workshop_item_get_file_id:
 * @self: an #LrgWorkshopItem
 *
 * Gets the Workshop file ID.
 *
 * Returns: the file ID
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_workshop_item_get_file_id (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_title:
 * @self: an #LrgWorkshopItem
 *
 * Gets the item's title.
 *
 * Returns: (transfer none) (nullable): the title
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_workshop_item_get_title (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_set_title:
 * @self: an #LrgWorkshopItem
 * @title: (nullable): the new title
 *
 * Sets the item's title.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_item_set_title (LrgWorkshopItem *self,
                             const gchar     *title);

/**
 * lrg_workshop_item_get_description:
 * @self: an #LrgWorkshopItem
 *
 * Gets the item's description.
 *
 * Returns: (transfer none) (nullable): the description
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_workshop_item_get_description (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_set_description:
 * @self: an #LrgWorkshopItem
 * @description: (nullable): the new description
 *
 * Sets the item's description.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_item_set_description (LrgWorkshopItem *self,
                                   const gchar     *description);

/**
 * lrg_workshop_item_get_owner_id:
 * @self: an #LrgWorkshopItem
 *
 * Gets the Steam ID of the item's owner.
 *
 * Returns: the owner's Steam ID
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_workshop_item_get_owner_id (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_time_created:
 * @self: an #LrgWorkshopItem
 *
 * Gets the creation timestamp.
 *
 * Returns: Unix timestamp of creation, or 0 if unknown
 */
LRG_AVAILABLE_IN_ALL
guint32
lrg_workshop_item_get_time_created (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_time_updated:
 * @self: an #LrgWorkshopItem
 *
 * Gets the last update timestamp.
 *
 * Returns: Unix timestamp of last update, or 0 if unknown
 */
LRG_AVAILABLE_IN_ALL
guint32
lrg_workshop_item_get_time_updated (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_visibility:
 * @self: an #LrgWorkshopItem
 *
 * Gets the item's visibility setting.
 *
 * Returns: the visibility
 */
LRG_AVAILABLE_IN_ALL
LrgWorkshopItemVisibility
lrg_workshop_item_get_visibility (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_set_visibility:
 * @self: an #LrgWorkshopItem
 * @visibility: the new visibility
 *
 * Sets the item's visibility setting.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_item_set_visibility (LrgWorkshopItem          *self,
                                  LrgWorkshopItemVisibility visibility);

/**
 * lrg_workshop_item_get_tags:
 * @self: an #LrgWorkshopItem
 *
 * Gets the item's tags as a NULL-terminated array.
 *
 * Returns: (transfer none) (nullable) (array zero-terminated=1): the tags
 */
LRG_AVAILABLE_IN_ALL
const gchar * const *
lrg_workshop_item_get_tags (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_set_tags:
 * @self: an #LrgWorkshopItem
 * @tags: (nullable) (array zero-terminated=1): the new tags
 *
 * Sets the item's tags.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_item_set_tags (LrgWorkshopItem      *self,
                            const gchar * const  *tags);

/**
 * lrg_workshop_item_add_tag:
 * @self: an #LrgWorkshopItem
 * @tag: the tag to add
 *
 * Adds a tag to the item.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_workshop_item_add_tag (LrgWorkshopItem *self,
                           const gchar     *tag);

/**
 * lrg_workshop_item_remove_tag:
 * @self: an #LrgWorkshopItem
 * @tag: the tag to remove
 *
 * Removes a tag from the item.
 *
 * Returns: %TRUE if the tag was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_item_remove_tag (LrgWorkshopItem *self,
                              const gchar     *tag);

/**
 * lrg_workshop_item_get_votes_up:
 * @self: an #LrgWorkshopItem
 *
 * Gets the number of upvotes.
 *
 * Returns: the upvote count
 */
LRG_AVAILABLE_IN_ALL
guint32
lrg_workshop_item_get_votes_up (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_votes_down:
 * @self: an #LrgWorkshopItem
 *
 * Gets the number of downvotes.
 *
 * Returns: the downvote count
 */
LRG_AVAILABLE_IN_ALL
guint32
lrg_workshop_item_get_votes_down (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_score:
 * @self: an #LrgWorkshopItem
 *
 * Gets the item's score (0.0 to 1.0).
 *
 * Returns: the score
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_workshop_item_get_score (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_state:
 * @self: an #LrgWorkshopItem
 *
 * Gets the current state flags for the item.
 *
 * Returns: the state flags
 */
LRG_AVAILABLE_IN_ALL
LrgWorkshopItemState
lrg_workshop_item_get_state (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_is_subscribed:
 * @self: an #LrgWorkshopItem
 *
 * Checks if the item is subscribed.
 *
 * Returns: %TRUE if subscribed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_item_is_subscribed (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_is_installed:
 * @self: an #LrgWorkshopItem
 *
 * Checks if the item is installed.
 *
 * Returns: %TRUE if installed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_item_is_installed (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_needs_update:
 * @self: an #LrgWorkshopItem
 *
 * Checks if the item needs an update.
 *
 * Returns: %TRUE if an update is available
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_item_needs_update (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_install_path:
 * @self: an #LrgWorkshopItem
 *
 * Gets the local installation path for the item.
 *
 * Returns: (transfer none) (nullable): the install path, or %NULL if not installed
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_workshop_item_get_install_path (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_file_size:
 * @self: an #LrgWorkshopItem
 *
 * Gets the file size in bytes.
 *
 * Returns: the file size
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_workshop_item_get_file_size (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_download_progress:
 * @self: an #LrgWorkshopItem
 * @bytes_downloaded: (out) (optional): bytes downloaded so far
 * @bytes_total: (out) (optional): total bytes to download
 *
 * Gets the download progress for an item that is currently downloading.
 *
 * Returns: %TRUE if download info is available
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_item_get_download_progress (LrgWorkshopItem *self,
                                         guint64         *bytes_downloaded,
                                         guint64         *bytes_total);

/**
 * lrg_workshop_item_is_banned:
 * @self: an #LrgWorkshopItem
 *
 * Checks if the item has been banned from the Workshop.
 *
 * Returns: %TRUE if banned
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_item_is_banned (LrgWorkshopItem *self);

/**
 * lrg_workshop_item_get_preview_url:
 * @self: an #LrgWorkshopItem
 *
 * Gets the URL for the item's preview image.
 *
 * Returns: (transfer none) (nullable): the preview URL
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_workshop_item_get_preview_url (LrgWorkshopItem *self);

G_END_DECLS

#endif /* LRG_WORKSHOP_ITEM_H */
