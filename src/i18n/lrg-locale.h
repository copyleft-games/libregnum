/* lrg-locale.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Locale object for internationalization.
 * Loads string tables from YAML files and supports pluralization.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "lrg-version.h"
#include "lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_LOCALE (lrg_locale_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgLocale, lrg_locale, LRG, LOCALE, GObject)

/**
 * LrgLocaleClass:
 * @parent_class: the parent class
 * @get_plural_form: virtual function to determine plural form from count
 *
 * Class structure for #LrgLocale.
 */
struct _LrgLocaleClass
{
    GObjectClass parent_class;

    /**
     * LrgLocaleClass::get_plural_form:
     * @self: the locale
     * @count: the count to determine plural form for
     *
     * Determines the appropriate plural form for a given count.
     * Subclasses can override to implement language-specific plural rules.
     *
     * Returns: the plural form to use
     */
    LrgPluralForm (*get_plural_form) (LrgLocale *self,
                                      gint       count);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_locale_new:
 * @code: locale code (e.g., "en", "en_US", "de_DE")
 * @name: human-readable name (e.g., "English", "Deutsch")
 *
 * Creates a new empty locale.
 *
 * Returns: (transfer full): a new #LrgLocale
 */
LRG_AVAILABLE_IN_ALL
LrgLocale *lrg_locale_new (const gchar *code,
                           const gchar *name);

/**
 * lrg_locale_new_from_file:
 * @path: path to YAML locale file
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new locale by loading strings from a YAML file.
 *
 * The YAML format should be:
 * ```yaml
 * code: en
 * name: English
 * strings:
 *   greeting: Hello
 *   farewell: Goodbye
 *   items:
 *     one: "%d item"
 *     other: "%d items"
 * ```
 *
 * Returns: (transfer full) (nullable): a new #LrgLocale, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgLocale *lrg_locale_new_from_file (const gchar  *path,
                                     GError      **error);

/**
 * lrg_locale_get_code:
 * @self: a #LrgLocale
 *
 * Gets the locale code.
 *
 * Returns: (transfer none): the locale code
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_locale_get_code (LrgLocale *self);

/**
 * lrg_locale_get_name:
 * @self: a #LrgLocale
 *
 * Gets the human-readable locale name.
 *
 * Returns: (transfer none): the locale name
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_locale_get_name (LrgLocale *self);

/**
 * lrg_locale_set_string:
 * @self: a #LrgLocale
 * @key: the string key
 * @value: the localized string value
 *
 * Sets a localized string for the given key.
 */
LRG_AVAILABLE_IN_ALL
void lrg_locale_set_string (LrgLocale   *self,
                            const gchar *key,
                            const gchar *value);

/**
 * lrg_locale_get_string:
 * @self: a #LrgLocale
 * @key: the string key to look up
 *
 * Gets a localized string by key.
 *
 * Returns: (transfer none) (nullable): the localized string, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_locale_get_string (LrgLocale   *self,
                                    const gchar *key);

/**
 * lrg_locale_set_plural:
 * @self: a #LrgLocale
 * @key: the base string key
 * @form: the plural form
 * @value: the localized string for this form
 *
 * Sets a pluralized string for the given key and form.
 */
LRG_AVAILABLE_IN_ALL
void lrg_locale_set_plural (LrgLocale     *self,
                            const gchar   *key,
                            LrgPluralForm  form,
                            const gchar   *value);

/**
 * lrg_locale_get_plural:
 * @self: a #LrgLocale
 * @key: the base string key
 * @count: the count to pluralize for
 *
 * Gets the appropriate pluralized string for the given count.
 * Falls back to "other" form if specific form not found.
 *
 * Returns: (transfer none) (nullable): the pluralized string, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
const gchar *lrg_locale_get_plural (LrgLocale   *self,
                                    const gchar *key,
                                    gint         count);

/**
 * lrg_locale_has_string:
 * @self: a #LrgLocale
 * @key: the string key to check
 *
 * Checks if the locale has a string for the given key.
 *
 * Returns: %TRUE if the key exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_locale_has_string (LrgLocale   *self,
                                const gchar *key);

/**
 * lrg_locale_get_plural_form:
 * @self: a #LrgLocale
 * @count: the count
 *
 * Gets the plural form to use for a given count.
 * Uses English plural rules by default (one vs other).
 *
 * Returns: the plural form
 */
LRG_AVAILABLE_IN_ALL
LrgPluralForm lrg_locale_get_plural_form (LrgLocale *self,
                                          gint       count);

/**
 * lrg_locale_get_string_count:
 * @self: a #LrgLocale
 *
 * Gets the number of strings in the locale.
 *
 * Returns: the number of strings
 */
LRG_AVAILABLE_IN_ALL
guint lrg_locale_get_string_count (LrgLocale *self);

/**
 * lrg_locale_get_keys:
 * @self: a #LrgLocale
 *
 * Gets all string keys in the locale.
 *
 * Returns: (transfer container) (element-type utf8): array of keys
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *lrg_locale_get_keys (LrgLocale *self);

G_END_DECLS
