/* lrg-localization.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the singleton localization manager.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_I18N

#include "lrg-localization.h"
#include "lrg-log.h"
#include <stdarg.h>

struct _LrgLocalization
{
    GObject     parent_instance;

    GHashTable *locales;       /* code -> LrgLocale */
    LrgLocale  *current;       /* current locale (not owned) */
    LrgLocale  *fallback;      /* fallback locale (not owned) */
};

G_DEFINE_TYPE (LrgLocalization, lrg_localization, G_TYPE_OBJECT)

enum
{
    SIGNAL_LOCALE_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* Singleton instance */
static LrgLocalization *default_instance = NULL;

/* ========================================================================== */
/* GObject Implementation                                                     */
/* ========================================================================== */

static void
lrg_localization_finalize (GObject *object)
{
    LrgLocalization *self;

    self = LRG_LOCALIZATION (object);

    g_clear_pointer (&self->locales, g_hash_table_unref);
    self->current = NULL;
    self->fallback = NULL;

    G_OBJECT_CLASS (lrg_localization_parent_class)->finalize (object);
}

static void
lrg_localization_class_init (LrgLocalizationClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_localization_finalize;

    /**
     * LrgLocalization::locale-changed:
     * @self: the localization manager
     * @old_code: (nullable): the previous locale code, or %NULL
     * @new_code: the new locale code
     *
     * Emitted when the current locale changes.
     */
    signals[SIGNAL_LOCALE_CHANGED] =
        g_signal_new ("locale-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_localization_init (LrgLocalization *self)
{
    self->locales = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, g_object_unref);
    self->current = NULL;
    self->fallback = NULL;
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

LrgLocalization *
lrg_localization_get_default (void)
{
    if (default_instance == NULL)
    {
        default_instance = g_object_new (LRG_TYPE_LOCALIZATION, NULL);
    }

    return default_instance;
}

void
lrg_localization_add_locale (LrgLocalization *self,
                             LrgLocale       *locale)
{
    const gchar *code;

    g_return_if_fail (LRG_IS_LOCALIZATION (self));
    g_return_if_fail (LRG_IS_LOCALE (locale));

    code = lrg_locale_get_code (locale);
    g_hash_table_replace (self->locales, g_strdup (code), locale);

    lrg_log_debug ("Added locale '%s' (%s)",
                   code, lrg_locale_get_name (locale));

    /* Set as current if no current locale */
    if (self->current == NULL)
    {
        self->current = locale;
    }
}

gboolean
lrg_localization_add_locale_from_file (LrgLocalization  *self,
                                       const gchar      *path,
                                       GError          **error)
{
    LrgLocale *locale;

    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    locale = lrg_locale_new_from_file (path, error);
    if (locale == NULL)
        return FALSE;

    lrg_localization_add_locale (self, locale);
    return TRUE;
}

gboolean
lrg_localization_remove_locale (LrgLocalization *self,
                                const gchar     *code)
{
    LrgLocale *locale;

    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    locale = (LrgLocale *)g_hash_table_lookup (self->locales, code);
    if (locale == NULL)
        return FALSE;

    /* Clear current/fallback references if needed */
    if (self->current == locale)
        self->current = NULL;
    if (self->fallback == locale)
        self->fallback = NULL;

    g_hash_table_remove (self->locales, code);
    return TRUE;
}

LrgLocale *
lrg_localization_get_locale (LrgLocalization *self,
                             const gchar     *code)
{
    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), NULL);
    g_return_val_if_fail (code != NULL, NULL);

    return (LrgLocale *)g_hash_table_lookup (self->locales, code);
}

LrgLocale *
lrg_localization_get_current (LrgLocalization *self)
{
    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), NULL);

    return self->current;
}

const gchar *
lrg_localization_get_current_code (LrgLocalization *self)
{
    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), NULL);

    if (self->current == NULL)
        return NULL;

    return lrg_locale_get_code (self->current);
}

gboolean
lrg_localization_set_current (LrgLocalization *self,
                              const gchar     *code)
{
    LrgLocale *locale;
    const gchar *old_code;

    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    locale = (LrgLocale *)g_hash_table_lookup (self->locales, code);
    if (locale == NULL)
        return FALSE;

    if (self->current == locale)
        return TRUE;  /* Already current */

    old_code = self->current ? lrg_locale_get_code (self->current) : NULL;
    self->current = locale;

    lrg_log_debug ("Locale changed from '%s' to '%s'",
                   old_code ? old_code : "(none)", code);

    g_signal_emit (self, signals[SIGNAL_LOCALE_CHANGED], 0, old_code, code);

    return TRUE;
}

LrgLocale *
lrg_localization_get_fallback (LrgLocalization *self)
{
    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), NULL);

    return self->fallback;
}

gboolean
lrg_localization_set_fallback (LrgLocalization *self,
                               const gchar     *code)
{
    LrgLocale *locale;

    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    locale = (LrgLocale *)g_hash_table_lookup (self->locales, code);
    if (locale == NULL)
        return FALSE;

    self->fallback = locale;
    return TRUE;
}

const gchar *
lrg_localization_get (LrgLocalization *self,
                      const gchar     *key)
{
    const gchar *result;

    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    /* Try current locale first */
    if (self->current != NULL)
    {
        result = lrg_locale_get_string (self->current, key);
        if (result != NULL)
            return result;
    }

    /* Try fallback locale */
    if (self->fallback != NULL && self->fallback != self->current)
    {
        result = lrg_locale_get_string (self->fallback, key);
        if (result != NULL)
            return result;
    }

    return NULL;
}

const gchar *
lrg_localization_get_plural (LrgLocalization *self,
                             const gchar     *key,
                             gint             count)
{
    const gchar *result;

    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    /* Try current locale first */
    if (self->current != NULL)
    {
        result = lrg_locale_get_plural (self->current, key, count);
        if (result != NULL)
            return result;
    }

    /* Try fallback locale */
    if (self->fallback != NULL && self->fallback != self->current)
    {
        result = lrg_locale_get_plural (self->fallback, key, count);
        if (result != NULL)
            return result;
    }

    return NULL;
}

gchar *
lrg_localization_format (LrgLocalization *self,
                         const gchar     *key,
                         ...)
{
    const gchar *format;
    va_list args;
    gchar *result;

    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    format = lrg_localization_get (self, key);
    if (format == NULL)
        return NULL;

    va_start (args, key);
    result = g_strdup_vprintf (format, args);
    va_end (args);

    return result;
}

gchar *
lrg_localization_format_plural (LrgLocalization *self,
                                const gchar     *key,
                                gint             count,
                                ...)
{
    const gchar *format;
    va_list args;
    gchar *result;

    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    format = lrg_localization_get_plural (self, key, count);
    if (format == NULL)
        return NULL;

    va_start (args, count);
    result = g_strdup_vprintf (format, args);
    va_end (args);

    return result;
}

gboolean
lrg_localization_has_locale (LrgLocalization *self,
                             const gchar     *code)
{
    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), FALSE);
    g_return_val_if_fail (code != NULL, FALSE);

    return g_hash_table_contains (self->locales, code);
}

GPtrArray *
lrg_localization_get_locale_codes (LrgLocalization *self)
{
    GPtrArray *codes;
    GHashTableIter iter;
    gpointer key_ptr;

    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), NULL);

    codes = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->locales);
    while (g_hash_table_iter_next (&iter, &key_ptr, NULL))
    {
        g_ptr_array_add (codes, key_ptr);
    }

    return codes;
}

guint
lrg_localization_get_locale_count (LrgLocalization *self)
{
    g_return_val_if_fail (LRG_IS_LOCALIZATION (self), 0);

    return g_hash_table_size (self->locales);
}
