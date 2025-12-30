/* lrg-dlc.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * DLC (Downloadable Content) representation.
 *
 * LrgDlc extends LrgMod with ownership verification, platform
 * integration, and content gating support. DLC is treated as
 * a specialized type of mod.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "../mod/lrg-mod.h"
#include "lrg-dlc-ownership.h"

G_BEGIN_DECLS

#define LRG_TYPE_DLC (lrg_dlc_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgDlc, lrg_dlc, LRG, DLC, LrgMod)

/**
 * LrgDlcClass:
 * @parent_class: The parent class
 * @verify_ownership: Virtual method to verify DLC ownership.
 *                    Subclasses can override for custom verification.
 * @get_trial_content_ids: Virtual method to get trial content IDs.
 *                         Subclasses can override to provide content lists.
 * @get_store_url: Virtual method to get the store URL.
 *                 Subclasses can override for platform-specific URLs.
 *
 * The class structure for #LrgDlc.
 */
struct _LrgDlcClass
{
    LrgModClass parent_class;

    /*< public >*/

    /* Virtual methods for subclasses */
    LrgDlcOwnershipState (* verify_ownership)      (LrgDlc   *self,
                                                     GError  **error);
    GPtrArray *          (* get_trial_content_ids) (LrgDlc   *self);
    gchar *              (* get_store_url)         (LrgDlc   *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_dlc_new:
 * @manifest: the mod manifest
 * @base_path: the DLC's base directory path
 * @dlc_type: the type of DLC
 *
 * Creates a new DLC from a manifest.
 *
 * Returns: (transfer full): a new #LrgDlc
 */
LRG_AVAILABLE_IN_ALL
LrgDlc * lrg_dlc_new (LrgModManifest *manifest,
                      const gchar    *base_path,
                      LrgDlcType      dlc_type);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_dlc_get_dlc_type:
 * @self: a #LrgDlc
 *
 * Gets the DLC type.
 *
 * Returns: the #LrgDlcType
 */
LRG_AVAILABLE_IN_ALL
LrgDlcType lrg_dlc_get_dlc_type (LrgDlc *self);

/**
 * lrg_dlc_get_price_string:
 * @self: a #LrgDlc
 *
 * Gets the display price string.
 *
 * Returns: (transfer none) (nullable): the price string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dlc_get_price_string (LrgDlc *self);

/**
 * lrg_dlc_set_price_string:
 * @self: a #LrgDlc
 * @price_string: the price string (e.g., "$14.99")
 *
 * Sets the display price string.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_set_price_string (LrgDlc      *self,
                                const gchar *price_string);

/**
 * lrg_dlc_get_currency:
 * @self: a #LrgDlc
 *
 * Gets the currency code.
 *
 * Returns: (transfer none) (nullable): the currency code
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dlc_get_currency (LrgDlc *self);

/**
 * lrg_dlc_set_currency:
 * @self: a #LrgDlc
 * @currency: the currency code (e.g., "USD")
 *
 * Sets the currency code.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_set_currency (LrgDlc      *self,
                            const gchar *currency);

/**
 * lrg_dlc_get_steam_app_id:
 * @self: a #LrgDlc
 *
 * Gets the Steam App ID for this DLC.
 *
 * Returns: the Steam App ID, or 0 if not a Steam DLC
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_dlc_get_steam_app_id (LrgDlc *self);

/**
 * lrg_dlc_set_steam_app_id:
 * @self: a #LrgDlc
 * @app_id: the Steam App ID
 *
 * Sets the Steam App ID for this DLC.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_set_steam_app_id (LrgDlc *self,
                                guint32 app_id);

/**
 * lrg_dlc_get_store_id:
 * @self: a #LrgDlc
 *
 * Gets the generic store identifier.
 *
 * Returns: (transfer none) (nullable): the store ID
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dlc_get_store_id (LrgDlc *self);

/**
 * lrg_dlc_set_store_id:
 * @self: a #LrgDlc
 * @store_id: the store identifier
 *
 * Sets the generic store identifier.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_set_store_id (LrgDlc      *self,
                            const gchar *store_id);

/**
 * lrg_dlc_get_release_date:
 * @self: a #LrgDlc
 *
 * Gets the DLC release date.
 *
 * Returns: (transfer none) (nullable): the release date
 */
LRG_AVAILABLE_IN_ALL
GDateTime * lrg_dlc_get_release_date (LrgDlc *self);

/**
 * lrg_dlc_set_release_date:
 * @self: a #LrgDlc
 * @release_date: (nullable): the release date
 *
 * Sets the DLC release date.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_set_release_date (LrgDlc    *self,
                                GDateTime *release_date);

/**
 * lrg_dlc_get_min_game_version:
 * @self: a #LrgDlc
 *
 * Gets the minimum game version required for this DLC.
 *
 * Returns: (transfer none) (nullable): the minimum version string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_dlc_get_min_game_version (LrgDlc *self);

/**
 * lrg_dlc_set_min_game_version:
 * @self: a #LrgDlc
 * @version: the minimum version string
 *
 * Sets the minimum game version required.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_set_min_game_version (LrgDlc      *self,
                                    const gchar *version);

/**
 * lrg_dlc_get_trial_enabled:
 * @self: a #LrgDlc
 *
 * Gets whether trial mode is enabled for this DLC.
 *
 * Returns: %TRUE if trial mode is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dlc_get_trial_enabled (LrgDlc *self);

/**
 * lrg_dlc_set_trial_enabled:
 * @self: a #LrgDlc
 * @enabled: whether to enable trial mode
 *
 * Sets whether trial mode is enabled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_set_trial_enabled (LrgDlc   *self,
                                 gboolean  enabled);

/* ==========================================================================
 * Ownership
 * ========================================================================== */

/**
 * lrg_dlc_get_ownership_state:
 * @self: a #LrgDlc
 *
 * Gets the current ownership state.
 *
 * Returns: the #LrgDlcOwnershipState
 */
LRG_AVAILABLE_IN_ALL
LrgDlcOwnershipState lrg_dlc_get_ownership_state (LrgDlc *self);

/**
 * lrg_dlc_set_ownership_checker:
 * @self: a #LrgDlc
 * @checker: (nullable): the ownership checker to use
 *
 * Sets the ownership checker for this DLC.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_set_ownership_checker (LrgDlc          *self,
                                     LrgDlcOwnership *checker);

/**
 * lrg_dlc_get_ownership_checker:
 * @self: a #LrgDlc
 *
 * Gets the ownership checker for this DLC.
 *
 * Returns: (transfer none) (nullable): the ownership checker
 */
LRG_AVAILABLE_IN_ALL
LrgDlcOwnership * lrg_dlc_get_ownership_checker (LrgDlc *self);

/**
 * lrg_dlc_verify_ownership:
 * @self: a #LrgDlc
 * @error: (nullable): return location for a #GError
 *
 * Verifies ownership of this DLC.
 *
 * This updates the ownership-state property and emits the
 * ownership-changed signal if the state changes.
 *
 * Returns: the new #LrgDlcOwnershipState
 */
LRG_AVAILABLE_IN_ALL
LrgDlcOwnershipState lrg_dlc_verify_ownership (LrgDlc   *self,
                                                GError  **error);

/**
 * lrg_dlc_is_owned:
 * @self: a #LrgDlc
 *
 * Checks if the DLC is owned (either full or trial access).
 *
 * Returns: %TRUE if the DLC is accessible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dlc_is_owned (LrgDlc *self);

/* ==========================================================================
 * Trial Content
 * ========================================================================== */

/**
 * lrg_dlc_add_trial_content_id:
 * @self: a #LrgDlc
 * @content_id: the content identifier
 *
 * Adds a content ID that is accessible in trial mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_add_trial_content_id (LrgDlc      *self,
                                    const gchar *content_id);

/**
 * lrg_dlc_remove_trial_content_id:
 * @self: a #LrgDlc
 * @content_id: the content identifier
 *
 * Removes a content ID from trial access.
 */
LRG_AVAILABLE_IN_ALL
void lrg_dlc_remove_trial_content_id (LrgDlc      *self,
                                       const gchar *content_id);

/**
 * lrg_dlc_get_trial_content_ids:
 * @self: a #LrgDlc
 *
 * Gets the list of content IDs accessible in trial mode.
 *
 * Returns: (transfer none) (element-type utf8): array of content IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_dlc_get_trial_content_ids (LrgDlc *self);

/**
 * lrg_dlc_is_content_accessible:
 * @self: a #LrgDlc
 * @content_id: the content identifier to check
 *
 * Checks if specific content is accessible.
 *
 * Content is accessible if:
 * - The DLC is fully owned, OR
 * - The DLC has trial access and the content is in the trial list
 *
 * If content is not accessible and the user attempts to access it,
 * the purchase-prompted signal is emitted.
 *
 * Returns: %TRUE if the content is accessible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dlc_is_content_accessible (LrgDlc      *self,
                                         const gchar *content_id);

/* ==========================================================================
 * Store Integration
 * ========================================================================== */

/**
 * lrg_dlc_get_store_url:
 * @self: a #LrgDlc
 *
 * Gets the URL to the DLC's store page.
 *
 * The URL format depends on the platform (Steam, etc.).
 *
 * Returns: (transfer full) (nullable): the store URL, or %NULL
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_dlc_get_store_url (LrgDlc *self);

/**
 * lrg_dlc_open_store_page:
 * @self: a #LrgDlc
 * @error: (nullable): return location for a #GError
 *
 * Opens the DLC's store page in the platform's overlay or browser.
 *
 * Returns: %TRUE if the store page was opened successfully
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_dlc_open_store_page (LrgDlc   *self,
                                   GError  **error);

G_END_DECLS
