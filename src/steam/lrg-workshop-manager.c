/* lrg-workshop-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorkshopManager - Steam Workshop operations manager.
 */

#include "config.h"

#include "lrg-workshop-manager.h"
#include "lrg-steam-types.h"
#include "../lrg-log.h"

struct _LrgWorkshopManager
{
    GObject parent_instance;

    /* Configuration */
    guint32 app_id;

    /* Update state */
    gboolean updating;
#ifdef LRG_ENABLE_STEAM
    UGCUpdateHandle_t update_handle;
#else
    guint64 update_handle;
#endif
};

enum
{
    PROP_0,
    PROP_APP_ID,
    N_PROPS
};

enum
{
    SIGNAL_ITEM_SUBSCRIBED,
    SIGNAL_ITEM_UNSUBSCRIBED,
    SIGNAL_ITEM_INSTALLED,
    SIGNAL_ITEM_CREATED,
    SIGNAL_ITEM_UPDATED,
    SIGNAL_ITEM_DELETED,
    SIGNAL_QUERY_COMPLETED,
    SIGNAL_DOWNLOAD_PROGRESS,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

G_DEFINE_TYPE (LrgWorkshopManager, lrg_workshop_manager, G_TYPE_OBJECT)

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_workshop_manager_finalize (GObject *object)
{
    /* LrgWorkshopManager *self = LRG_WORKSHOP_MANAGER (object); */

    G_OBJECT_CLASS (lrg_workshop_manager_parent_class)->finalize (object);
}

static void
lrg_workshop_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgWorkshopManager *self = LRG_WORKSHOP_MANAGER (object);

    switch (prop_id)
    {
    case PROP_APP_ID:
        g_value_set_uint (value, self->app_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_workshop_manager_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgWorkshopManager *self = LRG_WORKSHOP_MANAGER (object);

    switch (prop_id)
    {
    case PROP_APP_ID:
        self->app_id = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_workshop_manager_class_init (LrgWorkshopManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_workshop_manager_finalize;
    object_class->get_property = lrg_workshop_manager_get_property;
    object_class->set_property = lrg_workshop_manager_set_property;

    /**
     * LrgWorkshopManager:app-id:
     *
     * The Steam application ID.
     */
    properties[PROP_APP_ID] =
        g_param_spec_uint ("app-id",
                           "App ID",
                           "Steam application ID",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgWorkshopManager::item-subscribed:
     * @self: the #LrgWorkshopManager
     * @file_id: the Workshop file ID
     * @success: whether the subscription succeeded
     *
     * Emitted when a subscription request completes.
     */
    signals[SIGNAL_ITEM_SUBSCRIBED] =
        g_signal_new ("item-subscribed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_UINT64, G_TYPE_BOOLEAN);

    /**
     * LrgWorkshopManager::item-unsubscribed:
     * @self: the #LrgWorkshopManager
     * @file_id: the Workshop file ID
     * @success: whether the unsubscription succeeded
     *
     * Emitted when an unsubscription request completes.
     */
    signals[SIGNAL_ITEM_UNSUBSCRIBED] =
        g_signal_new ("item-unsubscribed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_UINT64, G_TYPE_BOOLEAN);

    /**
     * LrgWorkshopManager::item-installed:
     * @self: the #LrgWorkshopManager
     * @file_id: the Workshop file ID
     *
     * Emitted when a subscribed item finishes downloading and installing.
     */
    signals[SIGNAL_ITEM_INSTALLED] =
        g_signal_new ("item-installed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_UINT64);

    /**
     * LrgWorkshopManager::item-created:
     * @self: the #LrgWorkshopManager
     * @file_id: the new Workshop file ID
     * @success: whether creation succeeded
     *
     * Emitted when a new Workshop item is created.
     */
    signals[SIGNAL_ITEM_CREATED] =
        g_signal_new ("item-created",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_UINT64, G_TYPE_BOOLEAN);

    /**
     * LrgWorkshopManager::item-updated:
     * @self: the #LrgWorkshopManager
     * @file_id: the Workshop file ID
     * @success: whether the update succeeded
     *
     * Emitted when an item update completes.
     */
    signals[SIGNAL_ITEM_UPDATED] =
        g_signal_new ("item-updated",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_UINT64, G_TYPE_BOOLEAN);

    /**
     * LrgWorkshopManager::item-deleted:
     * @self: the #LrgWorkshopManager
     * @file_id: the Workshop file ID
     * @success: whether deletion succeeded
     *
     * Emitted when an item is deleted.
     */
    signals[SIGNAL_ITEM_DELETED] =
        g_signal_new ("item-deleted",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_UINT64, G_TYPE_BOOLEAN);

    /**
     * LrgWorkshopManager::query-completed:
     * @self: the #LrgWorkshopManager
     * @items: (element-type LrgWorkshopItem): array of result items
     * @total_matching: total number of matching items
     * @success: whether the query succeeded
     *
     * Emitted when a query completes.
     */
    signals[SIGNAL_QUERY_COMPLETED] =
        g_signal_new ("query-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 3,
                      G_TYPE_PTR_ARRAY, G_TYPE_UINT, G_TYPE_BOOLEAN);

    /**
     * LrgWorkshopManager::download-progress:
     * @self: the #LrgWorkshopManager
     * @file_id: the Workshop file ID
     * @bytes_downloaded: bytes downloaded so far
     * @bytes_total: total bytes to download
     *
     * Emitted periodically during item download.
     */
    signals[SIGNAL_DOWNLOAD_PROGRESS] =
        g_signal_new ("download-progress",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 3,
                      G_TYPE_UINT64, G_TYPE_UINT64, G_TYPE_UINT64);
}

static void
lrg_workshop_manager_init (LrgWorkshopManager *self)
{
    self->app_id = 0;
    self->updating = FALSE;
    self->update_handle = 0;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgWorkshopManager *
lrg_workshop_manager_new (guint32 app_id)
{
    return g_object_new (LRG_TYPE_WORKSHOP_MANAGER,
                         "app-id", app_id,
                         NULL);
}

guint32
lrg_workshop_manager_get_app_id (LrgWorkshopManager *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), 0);

    return self->app_id;
}

GPtrArray *
lrg_workshop_manager_get_subscribed_items (LrgWorkshopManager *self)
{
    GPtrArray *items;

    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), NULL);

    items = g_ptr_array_new_with_free_func (g_object_unref);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc;
    guint32 count;
    PublishedFileId_t *file_ids;

    ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
        return items;

    count = SteamAPI_ISteamUGC_GetNumSubscribedItems (ugc);
    if (count == 0)
        return items;

    file_ids = g_new (PublishedFileId_t, count);
    count = SteamAPI_ISteamUGC_GetSubscribedItems (ugc, file_ids, count);

    for (guint32 i = 0; i < count; i++)
    {
        LrgWorkshopItem *item = lrg_workshop_item_new (file_ids[i]);

        /* Get item state */
        LrgWorkshopItemState state = lrg_workshop_manager_get_item_state (self, file_ids[i]);
        g_object_set (item, "state", state, NULL);

        /* Get install info if installed */
        if (state & LRG_WORKSHOP_ITEM_STATE_INSTALLED)
        {
            gchar *install_path = NULL;
            guint64 size = 0;
            guint32 timestamp = 0;

            if (lrg_workshop_manager_get_install_info (self, file_ids[i],
                                                       &size, &install_path, &timestamp))
            {
                g_object_set (item,
                              "install-path", install_path,
                              "file-size", size,
                              NULL);
                g_free (install_path);
            }
        }

        g_ptr_array_add (items, item);
    }

    g_free (file_ids);
#endif

    return items;
}

guint
lrg_workshop_manager_get_subscribed_count (LrgWorkshopManager *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), 0);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
        return 0;

    return SteamAPI_ISteamUGC_GetNumSubscribedItems (ugc);
#else
    return 0;
#endif
}

gboolean
lrg_workshop_manager_is_subscribed (LrgWorkshopManager *self,
                                    guint64             file_id)
{
    LrgWorkshopItemState state;

    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);

    state = lrg_workshop_manager_get_item_state (self, file_id);
    return (state & LRG_WORKSHOP_ITEM_STATE_SUBSCRIBED) != 0;
}

gboolean
lrg_workshop_manager_subscribe (LrgWorkshopManager  *self,
                                guint64              file_id,
                                GError             **error)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                     "Steam Workshop not available");
        return FALSE;
    }

    /* Note: The API call is asynchronous. In a real implementation,
     * you would need to handle Steam callbacks to emit the signal. */
    SteamAPI_ISteamUGC_SubscribeItem (ugc, file_id);

    lrg_debug (LRG_LOG_DOMAIN_STEAM, "Subscribing to Workshop item %" G_GUINT64_FORMAT, file_id);
    return TRUE;
#else
    g_set_error (error,
                 LRG_WORKSHOP_ERROR,
                 LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                 "Steam Workshop not available (built without STEAM=1)");
    return FALSE;
#endif
}

gboolean
lrg_workshop_manager_unsubscribe (LrgWorkshopManager  *self,
                                  guint64              file_id,
                                  GError             **error)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                     "Steam Workshop not available");
        return FALSE;
    }

    SteamAPI_ISteamUGC_UnsubscribeItem (ugc, file_id);

    lrg_debug (LRG_LOG_DOMAIN_STEAM, "Unsubscribing from Workshop item %" G_GUINT64_FORMAT, file_id);
    return TRUE;
#else
    g_set_error (error,
                 LRG_WORKSHOP_ERROR,
                 LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                 "Steam Workshop not available (built without STEAM=1)");
    return FALSE;
#endif
}

LrgWorkshopItemState
lrg_workshop_manager_get_item_state (LrgWorkshopManager *self,
                                     guint64             file_id)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), LRG_WORKSHOP_ITEM_STATE_NONE);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
        return LRG_WORKSHOP_ITEM_STATE_NONE;

    uint32_t state = SteamAPI_ISteamUGC_GetItemState (ugc, file_id);

    /* Map Steam state flags to our enum */
    LrgWorkshopItemState result = LRG_WORKSHOP_ITEM_STATE_NONE;

    if (state & k_EItemStateSubscribed)
        result |= LRG_WORKSHOP_ITEM_STATE_SUBSCRIBED;
    if (state & k_EItemStateLegacyItem)
        result |= LRG_WORKSHOP_ITEM_STATE_LEGACY;
    if (state & k_EItemStateInstalled)
        result |= LRG_WORKSHOP_ITEM_STATE_INSTALLED;
    if (state & k_EItemStateNeedsUpdate)
        result |= LRG_WORKSHOP_ITEM_STATE_NEEDS_UPDATE;
    if (state & k_EItemStateDownloading)
        result |= LRG_WORKSHOP_ITEM_STATE_DOWNLOADING;
    if (state & k_EItemStateDownloadPending)
        result |= LRG_WORKSHOP_ITEM_STATE_DOWNLOAD_PENDING;

    return result;
#else
    return LRG_WORKSHOP_ITEM_STATE_NONE;
#endif
}

gboolean
lrg_workshop_manager_get_install_info (LrgWorkshopManager  *self,
                                       guint64              file_id,
                                       guint64             *size_on_disk,
                                       gchar              **install_path,
                                       guint32             *timestamp)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
        return FALSE;

    uint64_t size = 0;
    char path_buffer[4096];
    uint32_t ts = 0;

    if (!SteamAPI_ISteamUGC_GetItemInstallInfo (ugc, file_id,
                                                 &size, path_buffer,
                                                 sizeof (path_buffer), &ts))
    {
        return FALSE;
    }

    if (size_on_disk != NULL)
        *size_on_disk = size;
    if (install_path != NULL)
        *install_path = g_strdup (path_buffer);
    if (timestamp != NULL)
        *timestamp = ts;

    return TRUE;
#else
    return FALSE;
#endif
}

gboolean
lrg_workshop_manager_download_item (LrgWorkshopManager  *self,
                                    guint64              file_id,
                                    gboolean             high_priority,
                                    GError             **error)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                     "Steam Workshop not available");
        return FALSE;
    }

    if (!SteamAPI_ISteamUGC_DownloadItem (ugc, file_id, high_priority))
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_DOWNLOAD,
                     "Failed to start download for item %" G_GUINT64_FORMAT, file_id);
        return FALSE;
    }

    lrg_debug (LRG_LOG_DOMAIN_STEAM, "Started download for Workshop item %" G_GUINT64_FORMAT, file_id);
    return TRUE;
#else
    g_set_error (error,
                 LRG_WORKSHOP_ERROR,
                 LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                 "Steam Workshop not available (built without STEAM=1)");
    return FALSE;
#endif
}

gboolean
lrg_workshop_manager_execute_query (LrgWorkshopManager  *self,
                                    LrgWorkshopQuery    *query,
                                    GError             **error)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);
    g_return_val_if_fail (LRG_IS_WORKSHOP_QUERY (query), FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                     "Steam Workshop not available");
        return FALSE;
    }

    UGCQueryHandle_t handle;
    LrgWorkshopContentType content_type;
    guint page;
    const gchar *search_text;
    GPtrArray *required_tags;
    GPtrArray *excluded_tags;
    guint i;

    content_type = lrg_workshop_query_get_content_type (query);
    page = lrg_workshop_query_get_page (query);

    if (lrg_workshop_query_is_user_query (query))
    {
        guint64 user_id = lrg_workshop_query_get_user_id (query);
        handle = SteamAPI_ISteamUGC_CreateQueryUserUGCRequest (
            ugc,
            (uint32_t)(user_id & 0xFFFFFFFF), /* Account ID from Steam ID */
            1, /* k_EUserUGCList_Published */
            content_type,
            0, /* k_EUserUGCListSortOrder_CreationOrderDesc */
            self->app_id,
            self->app_id,
            page);
    }
    else
    {
        handle = SteamAPI_ISteamUGC_CreateQueryAllUGCRequestPage (
            ugc,
            lrg_workshop_query_get_query_type (query),
            content_type,
            self->app_id,
            self->app_id,
            page);
    }

    if (handle == 0)
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_QUERY,
                     "Failed to create Workshop query");
        return FALSE;
    }

    /* Set search text if provided */
    search_text = lrg_workshop_query_get_search_text (query);
    if (search_text != NULL && search_text[0] != '\0')
    {
        SteamAPI_ISteamUGC_SetSearchText (ugc, handle, search_text);
    }

    /* Add required tags */
    required_tags = lrg_workshop_query_get_required_tags (query);
    for (i = 0; i < required_tags->len; i++)
    {
        const gchar *tag = g_ptr_array_index (required_tags, i);
        SteamAPI_ISteamUGC_AddRequiredTag (ugc, handle, tag);
    }

    /* Add excluded tags */
    excluded_tags = lrg_workshop_query_get_excluded_tags (query);
    for (i = 0; i < excluded_tags->len; i++)
    {
        const gchar *tag = g_ptr_array_index (excluded_tags, i);
        SteamAPI_ISteamUGC_AddExcludedTag (ugc, handle, tag);
    }

    /* Send the query - results come via Steam callback */
    SteamAPI_ISteamUGC_SendQueryUGCRequest (ugc, handle);

    lrg_debug (LRG_LOG_DOMAIN_STEAM, "Submitted Workshop query (page %u)", page);
    return TRUE;
#else
    g_set_error (error,
                 LRG_WORKSHOP_ERROR,
                 LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                 "Steam Workshop not available (built without STEAM=1)");
    return FALSE;
#endif
}

gboolean
lrg_workshop_manager_create_item (LrgWorkshopManager  *self,
                                  GError             **error)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                     "Steam Workshop not available");
        return FALSE;
    }

    SteamAPI_ISteamUGC_CreateItem (ugc, self->app_id, k_EWorkshopFileTypeCommunity);

    lrg_debug (LRG_LOG_DOMAIN_STEAM, "Creating new Workshop item for app %u", self->app_id);
    return TRUE;
#else
    g_set_error (error,
                 LRG_WORKSHOP_ERROR,
                 LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                 "Steam Workshop not available (built without STEAM=1)");
    return FALSE;
#endif
}

gboolean
lrg_workshop_manager_update_item (LrgWorkshopManager  *self,
                                  LrgWorkshopItem     *item,
                                  const gchar         *content_folder,
                                  const gchar         *preview_file,
                                  const gchar         *change_note,
                                  GError             **error)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);
    g_return_val_if_fail (LRG_IS_WORKSHOP_ITEM (item), FALSE);
    g_return_val_if_fail (content_folder != NULL, FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                     "Steam Workshop not available");
        return FALSE;
    }

    if (self->updating)
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_BUSY,
                     "An update is already in progress");
        return FALSE;
    }

    guint64 file_id = lrg_workshop_item_get_file_id (item);
    UGCUpdateHandle_t handle = SteamAPI_ISteamUGC_StartItemUpdate (ugc, self->app_id, file_id);

    if (handle == 0)
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_UPDATE,
                     "Failed to start item update");
        return FALSE;
    }

    /* Set title and description */
    const gchar *title = lrg_workshop_item_get_title (item);
    if (title != NULL)
        SteamAPI_ISteamUGC_SetItemTitle (ugc, handle, title);

    const gchar *description = lrg_workshop_item_get_description (item);
    if (description != NULL)
        SteamAPI_ISteamUGC_SetItemDescription (ugc, handle, description);

    /* Set visibility */
    LrgWorkshopItemVisibility visibility = lrg_workshop_item_get_visibility (item);
    SteamAPI_ISteamUGC_SetItemVisibility (ugc, handle, visibility);

    /* Set content folder */
    SteamAPI_ISteamUGC_SetItemContent (ugc, handle, content_folder);

    /* Set preview if provided */
    if (preview_file != NULL)
        SteamAPI_ISteamUGC_SetItemPreview (ugc, handle, preview_file);

    /* Submit update */
    SteamAPI_ISteamUGC_SubmitItemUpdate (ugc, handle, change_note);

    self->updating = TRUE;
    self->update_handle = handle;

    lrg_debug (LRG_LOG_DOMAIN_STEAM, "Started update for Workshop item %" G_GUINT64_FORMAT, file_id);
    return TRUE;
#else
    g_set_error (error,
                 LRG_WORKSHOP_ERROR,
                 LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                 "Steam Workshop not available (built without STEAM=1)");
    return FALSE;
#endif
}

gboolean
lrg_workshop_manager_delete_item (LrgWorkshopManager  *self,
                                  guint64              file_id,
                                  GError             **error)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
    {
        g_set_error (error,
                     LRG_WORKSHOP_ERROR,
                     LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                     "Steam Workshop not available");
        return FALSE;
    }

    SteamAPI_ISteamUGC_DeleteItem (ugc, file_id);

    lrg_debug (LRG_LOG_DOMAIN_STEAM, "Deleting Workshop item %" G_GUINT64_FORMAT, file_id);
    return TRUE;
#else
    g_set_error (error,
                 LRG_WORKSHOP_ERROR,
                 LRG_WORKSHOP_ERROR_NOT_AVAILABLE,
                 "Steam Workshop not available (built without STEAM=1)");
    return FALSE;
#endif
}

gboolean
lrg_workshop_manager_get_update_progress (LrgWorkshopManager *self,
                                          guint64            *bytes_processed,
                                          guint64            *bytes_total)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);

    if (!self->updating)
        return FALSE;

#ifdef LRG_ENABLE_STEAM
    ISteamUGC *ugc = SteamAPI_SteamUGC_v018 ();
    if (ugc == NULL)
        return FALSE;

    uint64_t processed = 0;
    uint64_t total = 0;

    int status = SteamAPI_ISteamUGC_GetItemUpdateProgress (ugc, self->update_handle,
                                                            &processed, &total);
    /* status is EItemUpdateStatus enum, 0 = invalid */
    if (status == 0)
        return FALSE;

    if (bytes_processed != NULL)
        *bytes_processed = processed;
    if (bytes_total != NULL)
        *bytes_total = total;

    return TRUE;
#else
    return FALSE;
#endif
}

gboolean
lrg_workshop_manager_is_updating (LrgWorkshopManager *self)
{
    g_return_val_if_fail (LRG_IS_WORKSHOP_MANAGER (self), FALSE);

    return self->updating;
}
