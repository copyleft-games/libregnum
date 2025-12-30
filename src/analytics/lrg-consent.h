/* lrg-consent.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgConsent - GDPR-compliant consent management.
 *
 * Tracks user consent for analytics and crash reporting,
 * persisting the settings to a YAML file.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_CONSENT (lrg_consent_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgConsent, lrg_consent, LRG, CONSENT, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_consent_new:
 * @storage_path: (nullable): path to persist consent settings
 *
 * Creates a new consent manager.
 * If @storage_path is provided, consent settings will be persisted to that file.
 *
 * Returns: (transfer full): a new #LrgConsent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgConsent *
lrg_consent_new (const gchar *storage_path);

/**
 * lrg_consent_get_default:
 *
 * Gets the default consent manager instance.
 * Creates it if it doesn't exist.
 *
 * Returns: (transfer none): the default #LrgConsent
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgConsent *
lrg_consent_get_default (void);

/* ==========================================================================
 * Consent Settings
 * ========================================================================== */

/**
 * lrg_consent_get_analytics_enabled:
 * @self: an #LrgConsent
 *
 * Gets whether analytics collection is enabled.
 *
 * Returns: %TRUE if analytics is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_consent_get_analytics_enabled (LrgConsent *self);

/**
 * lrg_consent_set_analytics_enabled:
 * @self: an #LrgConsent
 * @enabled: whether to enable analytics
 *
 * Sets whether analytics collection is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_consent_set_analytics_enabled (LrgConsent *self,
                                   gboolean    enabled);

/**
 * lrg_consent_get_crash_reporting_enabled:
 * @self: an #LrgConsent
 *
 * Gets whether crash reporting is enabled.
 *
 * Returns: %TRUE if crash reporting is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_consent_get_crash_reporting_enabled (LrgConsent *self);

/**
 * lrg_consent_set_crash_reporting_enabled:
 * @self: an #LrgConsent
 * @enabled: whether to enable crash reporting
 *
 * Sets whether crash reporting is enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_consent_set_crash_reporting_enabled (LrgConsent *self,
                                         gboolean    enabled);

/**
 * lrg_consent_get_consent_date:
 * @self: an #LrgConsent
 *
 * Gets when consent was last given or modified.
 *
 * Returns: (transfer none) (nullable): the consent date
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GDateTime *
lrg_consent_get_consent_date (LrgConsent *self);

/**
 * lrg_consent_get_consent_version:
 * @self: an #LrgConsent
 *
 * Gets the version of the consent form that was shown.
 *
 * Returns: the consent version
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_consent_get_consent_version (LrgConsent *self);

/**
 * lrg_consent_set_consent_version:
 * @self: an #LrgConsent
 * @version: the consent form version
 *
 * Sets the consent form version.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_consent_set_consent_version (LrgConsent *self,
                                 guint       version);

/* ==========================================================================
 * Convenience Methods
 * ========================================================================== */

/**
 * lrg_consent_set_all:
 * @self: an #LrgConsent
 * @enabled: whether to enable all consent options
 *
 * Sets all consent options to the same value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_consent_set_all (LrgConsent *self,
                     gboolean    enabled);

/**
 * lrg_consent_requires_prompt:
 * @self: an #LrgConsent
 *
 * Checks whether the user needs to be prompted for consent.
 * Returns %TRUE if no consent has been recorded yet.
 *
 * Returns: %TRUE if a consent prompt is needed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_consent_requires_prompt (LrgConsent *self);

/**
 * lrg_consent_requires_reprompt:
 * @self: an #LrgConsent
 * @current_version: the current consent form version
 *
 * Checks whether the user needs to be re-prompted due to a new consent version.
 *
 * Returns: %TRUE if a re-prompt is needed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_consent_requires_reprompt (LrgConsent *self,
                               guint       current_version);

/* ==========================================================================
 * Persistence
 * ========================================================================== */

/**
 * lrg_consent_load:
 * @self: an #LrgConsent
 * @error: (nullable): return location for error
 *
 * Loads consent settings from the storage file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_consent_load (LrgConsent  *self,
                  GError     **error);

/**
 * lrg_consent_save:
 * @self: an #LrgConsent
 * @error: (nullable): return location for error
 *
 * Saves consent settings to the storage file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_consent_save (LrgConsent  *self,
                  GError     **error);

G_END_DECLS
