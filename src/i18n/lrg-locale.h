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

/**
 * LrgPluralRule:
 * @LRG_PLURAL_RULE_ONE_OTHER: "one" for 1 (and -1), "other" for every
 *   other count (English, German, Dutch, the Scandinavian languages,
 *   Spanish, Italian, Greek, Hungarian, Turkish ...)
 * @LRG_PLURAL_RULE_NONE: a single form, "other" (Japanese, Chinese,
 *   Korean, Vietnamese, Thai, Indonesian ...)
 * @LRG_PLURAL_RULE_ZERO_ONE_OTHER: "one" for 0 and 1, "other" for the
 *   rest (French, Brazilian Portuguese)
 * @LRG_PLURAL_RULE_EAST_SLAVIC: "one" for counts ending in 1 but not 11,
 *   "few" for counts ending in 2-4 but not 12-14, "many" for the rest
 *   (Russian, Ukrainian, Belarusian, Serbian, Croatian, Bosnian)
 * @LRG_PLURAL_RULE_POLISH: "one" for 1, "few" for counts ending in 2-4
 *   but not 12-14, "many" for the rest
 * @LRG_PLURAL_RULE_CZECH: "one" for 1, "few" for 2-4, "other" for the
 *   rest (Czech, Slovak)
 *
 * Integer plural rules of the Unicode CLDR, grouped into the families
 * most languages share.  lrg_plural_rule_for_language() picks one from a
 * language code; lrg_locale_new() applies it.
 */
typedef enum
{
    LRG_PLURAL_RULE_ONE_OTHER,
    LRG_PLURAL_RULE_NONE,
    LRG_PLURAL_RULE_ZERO_ONE_OTHER,
    LRG_PLURAL_RULE_EAST_SLAVIC,
    LRG_PLURAL_RULE_POLISH,
    LRG_PLURAL_RULE_CZECH
} LrgPluralRule;

LRG_AVAILABLE_IN_ALL
GType lrg_plural_rule_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_PLURAL_RULE (lrg_plural_rule_get_type ())

/**
 * lrg_plural_rule_for_language:
 * @code: a language or locale code ("de", "pt_BR", "ru-RU", "fr_FR.UTF-8")
 *
 * Finds the plural rule of a language.  The language part of @code
 * decides (pt_BR and fr take #LRG_PLURAL_RULE_ZERO_ONE_OTHER, plain pt
 * #LRG_PLURAL_RULE_ONE_OTHER); unknown languages take
 * #LRG_PLURAL_RULE_ONE_OTHER.
 *
 * Returns: the plural rule
 */
LRG_AVAILABLE_IN_ALL
LrgPluralRule lrg_plural_rule_for_language (const gchar *code);

/**
 * lrg_plural_rule_select:
 * @rule: a plural rule
 * @count: the count
 *
 * Applies @rule to @count (negative counts use their absolute value).
 *
 * Returns: the plural form
 */
LRG_AVAILABLE_IN_ALL
LrgPluralForm lrg_plural_rule_select (LrgPluralRule rule,
                                      gint          count);

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

/**
 * lrg_locale_set_plural_rule:
 * @self: a #LrgLocale
 * @rule: the plural rule
 *
 * Sets the rule the default #LrgLocaleClass.get_plural_form applies.
 * lrg_locale_new() and lrg_locale_new_from_file() set it from the
 * locale code with lrg_plural_rule_for_language().
 */
LRG_AVAILABLE_IN_ALL
void lrg_locale_set_plural_rule (LrgLocale     *self,
                                 LrgPluralRule  rule);

/**
 * lrg_locale_get_plural_rule:
 * @self: a #LrgLocale
 *
 * Returns: the plural rule of the locale
 */
LRG_AVAILABLE_IN_ALL
LrgPluralRule lrg_locale_get_plural_rule (LrgLocale *self);

/**
 * lrg_locale_merge_csv:
 * @self: a #LrgLocale
 * @text: (array length=length) (element-type guint8): CSV text (UTF-8)
 * @length: length of @text in bytes, or -1 when it is nul-terminated
 * @error: return location for a #GError, or %NULL
 *
 * Merges a two-column string table (`Key,Value`, the layout Unity's
 * localization tables and Daggerfall Unity's translation packs export)
 * into the locale: each row sets the string of its key, replacing an
 * existing one.  A first row of exactly `Key,Value` is a header and
 * skipped; rows with an empty key are skipped; columns after the second
 * are ignored.  A key ending in `#zero`, `#one`, `#two`, `#few`, `#many`
 * or `#other` sets that plural form of the key before the `#` instead.
 * Line breaks inside quoted values are kept; a trailing line break of a
 * value is removed, as Daggerfall Unity does.
 *
 * The whole text is parsed before anything is applied: on error the
 * locale is unchanged.
 *
 * Returns: the number of rows applied, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_locale_merge_csv (LrgLocale    *self,
                           const gchar  *text,
                           gssize        length,
                           GError      **error);

/**
 * lrg_locale_merge_csv_file:
 * @self: a #LrgLocale
 * @path: path of a CSV string table
 * @error: return location for a #GError, or %NULL
 *
 * Reads @path and merges it with lrg_locale_merge_csv().
 *
 * Returns: the number of rows applied, or -1 with @error set
 */
LRG_AVAILABLE_IN_ALL
gint lrg_locale_merge_csv_file (LrgLocale    *self,
                                const gchar  *path,
                                GError      **error);

G_END_DECLS
