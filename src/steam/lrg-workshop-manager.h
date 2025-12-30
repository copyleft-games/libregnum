/* lrg-workshop-manager.h - Steam Workshop operations manager
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_WORKSHOP_MANAGER_H
#define LRG_WORKSHOP_MANAGER_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-workshop-item.h"
#include "lrg-workshop-query.h"

G_BEGIN_DECLS

#define LRG_TYPE_WORKSHOP_MANAGER (lrg_workshop_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWorkshopManager, lrg_workshop_manager, LRG, WORKSHOP_MANAGER, GObject)

/**
 * lrg_workshop_manager_new:
 * @app_id: the Steam application ID
 *
 * Creates a new Workshop manager for the given application.
 *
 * Returns: (transfer full): a new #LrgWorkshopManager
 */
LRG_AVAILABLE_IN_ALL
LrgWorkshopManager *
lrg_workshop_manager_new (guint32 app_id);

/**
 * lrg_workshop_manager_get_app_id:
 * @self: an #LrgWorkshopManager
 *
 * Gets the Steam application ID.
 *
 * Returns: the app ID
 */
LRG_AVAILABLE_IN_ALL
guint32
lrg_workshop_manager_get_app_id (LrgWorkshopManager *self);

/* ==========================================================================
 * Subscribed Items
 * ========================================================================== */

/**
 * lrg_workshop_manager_get_subscribed_items:
 * @self: an #LrgWorkshopManager
 *
 * Gets all subscribed Workshop items for this application.
 *
 * Returns: (transfer full) (element-type LrgWorkshopItem): list of subscribed items
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_workshop_manager_get_subscribed_items (LrgWorkshopManager *self);

/**
 * lrg_workshop_manager_get_subscribed_count:
 * @self: an #LrgWorkshopManager
 *
 * Gets the number of subscribed Workshop items.
 *
 * Returns: the subscription count
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_workshop_manager_get_subscribed_count (LrgWorkshopManager *self);

/**
 * lrg_workshop_manager_is_subscribed:
 * @self: an #LrgWorkshopManager
 * @file_id: the Workshop file ID
 *
 * Checks if an item is subscribed.
 *
 * Returns: %TRUE if subscribed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_is_subscribed (LrgWorkshopManager *self,
                                    guint64             file_id);

/**
 * lrg_workshop_manager_subscribe:
 * @self: an #LrgWorkshopManager
 * @file_id: the Workshop file ID
 * @error: (nullable): return location for error
 *
 * Subscribes to a Workshop item. The result is delivered asynchronously
 * via the ::item-subscribed signal.
 *
 * Returns: %TRUE if the request was submitted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_subscribe (LrgWorkshopManager  *self,
                                guint64              file_id,
                                GError             **error);

/**
 * lrg_workshop_manager_unsubscribe:
 * @self: an #LrgWorkshopManager
 * @file_id: the Workshop file ID
 * @error: (nullable): return location for error
 *
 * Unsubscribes from a Workshop item. The result is delivered asynchronously
 * via the ::item-unsubscribed signal.
 *
 * Returns: %TRUE if the request was submitted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_unsubscribe (LrgWorkshopManager  *self,
                                  guint64              file_id,
                                  GError             **error);

/* ==========================================================================
 * Item State
 * ========================================================================== */

/**
 * lrg_workshop_manager_get_item_state:
 * @self: an #LrgWorkshopManager
 * @file_id: the Workshop file ID
 *
 * Gets the current state of a Workshop item.
 *
 * Returns: the item state flags
 */
LRG_AVAILABLE_IN_ALL
LrgWorkshopItemState
lrg_workshop_manager_get_item_state (LrgWorkshopManager *self,
                                     guint64             file_id);

/**
 * lrg_workshop_manager_get_install_info:
 * @self: an #LrgWorkshopManager
 * @file_id: the Workshop file ID
 * @size_on_disk: (out) (optional): size on disk in bytes
 * @install_path: (out) (optional) (transfer full): installation path
 * @timestamp: (out) (optional): last update timestamp
 *
 * Gets installation information for a Workshop item.
 *
 * Returns: %TRUE if the item is installed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_get_install_info (LrgWorkshopManager  *self,
                                       guint64              file_id,
                                       guint64             *size_on_disk,
                                       gchar              **install_path,
                                       guint32             *timestamp);

/**
 * lrg_workshop_manager_download_item:
 * @self: an #LrgWorkshopManager
 * @file_id: the Workshop file ID
 * @high_priority: whether to download with high priority
 * @error: (nullable): return location for error
 *
 * Requests download of a Workshop item.
 *
 * Returns: %TRUE if the download was started
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_download_item (LrgWorkshopManager  *self,
                                    guint64              file_id,
                                    gboolean             high_priority,
                                    GError             **error);

/* ==========================================================================
 * Queries
 * ========================================================================== */

/**
 * lrg_workshop_manager_execute_query:
 * @self: an #LrgWorkshopManager
 * @query: the query to execute
 * @error: (nullable): return location for error
 *
 * Executes a Workshop query. Results are delivered asynchronously
 * via the ::query-completed signal.
 *
 * Returns: %TRUE if the query was submitted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_execute_query (LrgWorkshopManager  *self,
                                    LrgWorkshopQuery    *query,
                                    GError             **error);

/* ==========================================================================
 * Publishing
 * ========================================================================== */

/**
 * lrg_workshop_manager_create_item:
 * @self: an #LrgWorkshopManager
 * @error: (nullable): return location for error
 *
 * Creates a new Workshop item. The file ID is delivered asynchronously
 * via the ::item-created signal.
 *
 * Returns: %TRUE if the request was submitted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_create_item (LrgWorkshopManager  *self,
                                  GError             **error);

/**
 * lrg_workshop_manager_update_item:
 * @self: an #LrgWorkshopManager
 * @item: the item to update
 * @content_folder: path to the content folder
 * @preview_file: (nullable): path to the preview image
 * @change_note: (nullable): change notes for this update
 * @error: (nullable): return location for error
 *
 * Updates a Workshop item's content and metadata.
 * Results are delivered via the ::item-updated signal.
 *
 * Returns: %TRUE if the update was started
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_update_item (LrgWorkshopManager  *self,
                                  LrgWorkshopItem     *item,
                                  const gchar         *content_folder,
                                  const gchar         *preview_file,
                                  const gchar         *change_note,
                                  GError             **error);

/**
 * lrg_workshop_manager_delete_item:
 * @self: an #LrgWorkshopManager
 * @file_id: the Workshop file ID
 * @error: (nullable): return location for error
 *
 * Deletes a Workshop item. The result is delivered asynchronously
 * via the ::item-deleted signal.
 *
 * Returns: %TRUE if the request was submitted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_delete_item (LrgWorkshopManager  *self,
                                  guint64              file_id,
                                  GError             **error);

/* ==========================================================================
 * Update Progress
 * ========================================================================== */

/**
 * lrg_workshop_manager_get_update_progress:
 * @self: an #LrgWorkshopManager
 * @bytes_processed: (out) (optional): bytes processed so far
 * @bytes_total: (out) (optional): total bytes to process
 *
 * Gets the progress of the current item update.
 *
 * Returns: %TRUE if an update is in progress
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_get_update_progress (LrgWorkshopManager *self,
                                          guint64            *bytes_processed,
                                          guint64            *bytes_total);

/**
 * lrg_workshop_manager_is_updating:
 * @self: an #LrgWorkshopManager
 *
 * Checks if an item update is in progress.
 *
 * Returns: %TRUE if updating
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_workshop_manager_is_updating (LrgWorkshopManager *self);

G_END_DECLS

#endif /* LRG_WORKSHOP_MANAGER_H */
