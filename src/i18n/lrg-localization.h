/* lrg-localization.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Singleton localization manager for internationalization.
 * Manages multiple locales and provides string lookup functions.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-locale.h"

G_BEGIN_DECLS

#define LRG_TYPE_LOCALIZATION (lrg_localization_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLocalization, lrg_localization, LRG, LOCALIZATION, GObject)

/**
 * lrg_localization_get_default:
 *
 * Gets the default localization manager instance.
 *
 * Returns: (transfer none): the default #LrgLocalization
 */
LRG_AVAILABLE_IN_ALL
LrgLocalization *lrg_localization_get_default (void);

/**
 * lrg_localization_add_locale:
 * @self: a #LrgLocalization
 * @locale: (transfer full): the locale to add
 *
 * Adds a locale to the manager.
 * If a locale with the same code already exists, it is replaced.
 */
LRG_AVAILABLE_IN_ALL
void lrg_localization_add_locale (LrgLocalization *self,
                                  LrgLocale       *locale);

/**
 * lrg_localization_add_locale_from_file:
 * @self: a #LrgLocalization
 * @path: path to the locale YAML file
 * @error: return location for a #GError, or %NULL
 *
 * Loads a locale from file and adds it to the manager.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_localization_add_locale_from_file (LrgLocalization  *self,
                                                const gchar      *path,
                                                GError          **error);

/**
 * lrg_localization_remove_locale:
 * @self: a #LrgLocalization
 * @code: the locale code to remove
 *
 * Removes a locale from the manager.
 *
 * Returns: %TRUE if the locale was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_localization_remove_locale (LrgLocalization *self,
                                         const gchar     *code);

/**
 * lrg_localization_get_locale:
 * @self: a #LrgLocalization
 * @code: the locale code to look up
 *
 * Gets a locale by code.
 *
 * Returns: (transfer none) (nullable): the locale, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgLocale *lrg_localization_get_locale (LrgLocalization *self,
                                        const gchar     *code);

/**
 * lrg_localization_get_current:
 * @self: a #LrgLocalization
 *
 * Gets the currently active locale.
 *
 * Returns: (transfer none) (nullable): the current locale
 */
LRG_AVAILABLE_IN_ALL
LrgLocale *lrg_localization_get_current (LrgLocalization *self);

/**
 * lrg_localization_get_current_code:
 * @self: a #LrgLocalization
 *
 * Gets the code of the currently active locale.
 *
 * Returns: (transfer none) (nullable): the current locale code
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_localization_get_current_code (LrgLocalization *self);

/**
 * lrg_localization_set_current:
 * @self: a #LrgLocalization
 * @code: the locale code to set as current
 *
 * Sets the current locale by code.
 * Emits the "locale-changed" signal if the locale changes.
 *
 * Returns: %TRUE if the locale was found and set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_localization_set_current (LrgLocalization *self,
                                       const gchar     *code);

/**
 * lrg_localization_get_fallback:
 * @self: a #LrgLocalization
 *
 * Gets the fallback locale used when strings aren't found
 * in the current locale.
 *
 * Returns: (transfer none) (nullable): the fallback locale
 */
LRG_AVAILABLE_IN_ALL
LrgLocale *lrg_localization_get_fallback (LrgLocalization *self);

/**
 * lrg_localization_set_fallback:
 * @self: a #LrgLocalization
 * @code: the locale code to set as fallback
 *
 * Sets the fallback locale by code.
 *
 * Returns: %TRUE if the locale was found and set
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_localization_set_fallback (LrgLocalization *self,
                                        const gchar     *code);

/**
 * lrg_localization_get:
 * @self: a #LrgLocalization
 * @key: the string key to look up
 *
 * Gets a localized string from the current locale.
 * Falls back to the fallback locale if not found.
 *
 * Returns: (transfer none) (nullable): the localized string, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_localization_get (LrgLocalization *self,
                                   const gchar     *key);

/**
 * lrg_localization_get_plural:
 * @self: a #LrgLocalization
 * @key: the string key
 * @count: the count for pluralization
 *
 * Gets a pluralized string from the current locale.
 *
 * Returns: (transfer none) (nullable): the pluralized string, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_localization_get_plural (LrgLocalization *self,
                                          const gchar     *key,
                                          gint             count);

/**
 * lrg_localization_format:
 * @self: a #LrgLocalization
 * @key: the string key
 * @...: format arguments
 *
 * Gets a localized string and formats it with the given arguments.
 * The string should contain printf-style format specifiers.
 *
 * Returns: (transfer full) (nullable): a newly allocated formatted string
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_localization_format (LrgLocalization *self,
                                const gchar     *key,
                                ...);

/**
 * lrg_localization_format_plural:
 * @self: a #LrgLocalization
 * @key: the string key
 * @count: the count for pluralization
 * @...: format arguments
 *
 * Gets a pluralized string and formats it with the given arguments.
 *
 * Returns: (transfer full) (nullable): a newly allocated formatted string
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_localization_format_plural (LrgLocalization *self,
                                       const gchar     *key,
                                       gint             count,
                                       ...);

/**
 * lrg_localization_has_locale:
 * @self: a #LrgLocalization
 * @code: the locale code to check
 *
 * Checks if a locale with the given code exists.
 *
 * Returns: %TRUE if the locale exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_localization_has_locale (LrgLocalization *self,
                                      const gchar     *code);

/**
 * lrg_localization_get_locale_codes:
 * @self: a #LrgLocalization
 *
 * Gets all available locale codes.
 *
 * Returns: (transfer container) (element-type utf8): array of locale codes
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_localization_get_locale_codes (LrgLocalization *self);

/**
 * lrg_localization_get_locale_count:
 * @self: a #LrgLocalization
 *
 * Gets the number of registered locales.
 *
 * Returns: the number of locales
 */
LRG_AVAILABLE_IN_ALL
guint lrg_localization_get_locale_count (LrgLocalization *self);

G_END_DECLS
