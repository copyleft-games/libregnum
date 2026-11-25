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
#include <string.h>

struct _LrgLocalization
{
    GObject     parent_instance;

    GHashTable *locales;       /* code -> LrgLocale */
    LrgLocale  *current;       /* current locale (not owned) */
    LrgLocale  *fallback;      /* fallback locale (not owned) */
    GHashTable *rejected;      /* "<locale>\x1f<key>" already warned about */
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
    g_clear_pointer (&self->rejected, g_hash_table_unref);
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
    self->rejected = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

/* ========================================================================== */
/* Format checking                                                            */
/* ========================================================================== */

/*
 * A translated string is a printf format the program never wrote: a
 * translator's "%s" where the source has "%d" reads an int as a pointer,
 * and "%n" writes through one.  Before a format from a locale is used,
 * its conversions are checked against the fallback locale's string for
 * the same key (the program's own text):
 *
 *   - every argument the translation consumes must have the type the
 *     source's argument at that position has; `*` widths and precisions
 *     consume an int; positional arguments (%2$s) are compared by position
 *   - it may consume fewer arguments than the source (a singular form
 *     omitting the number), never more
 *   - %n is never accepted, mixing positional and sequential conversions
 *     never, and a malformed conversion never
 *
 * A rejected translation is reported once per locale and key, and the
 * fallback's string is used instead.
 */

#define FORMAT_MAX_ARGS (32)

typedef struct
{
	gchar types[FORMAT_MAX_ARGS][4];   /* per argument position, "" = unused */
	guint count;                       /* highest position used */
} FormatArgs;

/* format_set_arg: record that argument @pos (1-based) has @type */
static gboolean
format_set_arg (FormatArgs  *args,
                guint        pos,
                const gchar *type)
{
	if (pos == 0 || pos > FORMAT_MAX_ARGS)
		return FALSE;
	if (args->types[pos - 1][0] != '\0' && g_strcmp0 (args->types[pos - 1], type) != 0)
		return FALSE;   /* one argument read as two types */
	g_strlcpy (args->types[pos - 1], type, sizeof (args->types[pos - 1]));
	if (pos > args->count)
		args->count = pos;
	return TRUE;
}

/* format_number: digits at *p (advanced past them), 0 if none */
static guint
format_number (const gchar **p)
{
	guint n = 0;

	while (g_ascii_isdigit (**p) && n < 100000)
	{
		n = n * 10 + (guint) (**p - '0');
		(*p)++;
	}
	return n;
}

/*
 * format_parse:
 * The arguments @format consumes, by position, with their va_arg type:
 * "i"/"li"/"lli"/"ji"/"zi"/"ti" integers (signedness does not change the
 * width), "f"/"Lf" floating point, "s" string, "ls" wide string, "p"
 * pointer.  FALSE for %n, an unknown or truncated conversion, or mixed
 * positional and sequential arguments.
 */
static gboolean
format_parse (const gchar *format,
              FormatArgs  *args)
{
	const gchar *p = format;
	guint next = 1;
	gint positional = -1;   /* -1 unknown, 0 sequential, 1 positional */

	memset (args, 0, sizeof (*args));
	while ((p = strchr (p, '%')) != NULL)
	{
		const gchar *start;
		gchar length[3] = { 0, 0, 0 };
		gchar type[4];
		guint pos = 0;
		guint n;
		gchar conv;

		p++;
		if (*p == '%')
		{
			p++;
			continue;
		}

		/* %m$ : positional */
		start = p;
		n = format_number (&p);
		if (n > 0 && *p == '$')
		{
			pos = n;
			p++;
		}
		else
			p = start;
		if (positional == -1)
			positional = pos > 0;
		else if (positional != (pos > 0))
			return FALSE;

		/* flags */
		while (*p != '\0' && strchr ("-+ #0'", *p) != NULL)
			p++;

		/* width and precision: digits, or * (an int argument) */
		for (n = 0; n < 2; n++)
		{
			if (n == 1)
			{
				if (*p != '.')
					break;
				p++;
			}
			if (*p == '*')
			{
				guint star = 0;

				p++;
				start = p;
				star = format_number (&p);
				if (star > 0 && *p == '$')
					p++;
				else
				{
					p = start;
					star = 0;
				}
				if (positional == 1 && star == 0)
					return FALSE;
				if (!format_set_arg (args, positional == 1 ? star : next++, "i"))
					return FALSE;
			}
			else
				(void) format_number (&p);
		}

		/* length modifier: hh and h promote to int, the rest widen */
		if (p[0] == 'h')
			p += (p[1] == 'h') ? 2 : 1;
		else if (p[0] == 'l' && p[1] == 'l')
		{
			length[0] = 'l';
			length[1] = 'l';
			p += 2;
		}
		else if (*p != '\0' && strchr ("ljztLq", *p) != NULL)
		{
			length[0] = (*p == 'q') ? 'l' : *p;
			if (*p == 'q')
				length[1] = 'l';
			p++;
		}

		/* the conversion */
		conv = *p;
		if (conv == '\0')
			return FALSE;
		p++;
		switch (conv)
		{
		case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'c':
			if (length[0] == 'L')
				return FALSE;
			g_snprintf (type, sizeof (type), "%si", length);
			break;
		case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': case 'a': case 'A':
			g_strlcpy (type, length[0] == 'L' ? "Lf" : "f", sizeof (type));
			break;
		case 's':
			g_strlcpy (type, length[0] == 'l' ? "ls" : "s", sizeof (type));
			break;
		case 'p':
			g_strlcpy (type, "p", sizeof (type));
			break;
		default:   /* %n among them: never */
			return FALSE;
		}
		if (!format_set_arg (args, pos > 0 ? pos : next++, type))
			return FALSE;
	}

	/* positional arguments must be 1..count with no gap (va_arg walks
	 * them in order) */
	for (next = 0; next < args->count; next++)
		if (args->types[next][0] == '\0')
			return FALSE;
	return TRUE;
}

/* format_compatible: @translation reads a prefix of @source's arguments
 * with the same types */
static gboolean
format_compatible (const gchar *translation,
                   const gchar *source)
{
	FormatArgs t;
	FormatArgs s;
	guint i;

	if (!format_parse (translation, &t) || !format_parse (source, &s))
		return FALSE;
	if (t.count > s.count)
		return FALSE;
	for (i = 0; i < t.count; i++)
		if (g_strcmp0 (t.types[i], s.types[i]) != 0)
			return FALSE;
	return TRUE;
}

/* format_reject: report a rejected translation once */
static void
format_reject (LrgLocalization *self,
               const gchar     *key,
               const gchar     *format)
{
	g_autofree gchar *id = NULL;
	const gchar *code = self->current != NULL ? lrg_locale_get_code (self->current) : "?";

	id = g_strdup_printf ("%s\x1f%s", code, key);
	if (g_hash_table_contains (self->rejected, id))
		return;
	lrg_warning (LRG_LOG_DOMAIN, "Locale '%s', key '%s': format \"%s\" does not match the "
	             "fallback's arguments (or uses %%n); using the fallback text", code, key, format);
	g_hash_table_add (self->rejected, g_steal_pointer (&id));
}

/*
 * checked_format:
 * The format to use for @key (@plural: for @count): the current locale's
 * when it is compatible with the fallback's, else the fallback's.  A
 * format with nothing to compare against (no fallback, or the key only in
 * the current locale) must still parse (no %n).
 */
static const gchar *
checked_format (LrgLocalization *self,
                const gchar     *key,
                gboolean         plural,
                gint             count)
{
	const gchar *format;
	const gchar *source = NULL;
	const gchar *source_other = NULL;
	FormatArgs   unused;

	format = plural ? lrg_localization_get_plural (self, key, count)
	                : lrg_localization_get (self, key);
	if (format == NULL)
		return NULL;

	if (self->fallback != NULL)
	{
		if (plural)
		{
			source = lrg_locale_get_plural (self->fallback, key, count);
			/* a singular translation may be checked against the plural
			 * source and vice versa: the forms differ by language */
			source_other = lrg_locale_get_plural (self->fallback, key, 1000000);
		}
		else
			source = lrg_locale_get_string (self->fallback, key);
	}

	if (source == NULL && source_other == NULL)
	{
		if (format_parse (format, &unused))
			return format;
		format_reject (self, key, format);
		return NULL;
	}
	if (format == source || format == source_other)
		return format_parse (format, &unused) ? format : NULL;
	if ((source != NULL && format_compatible (format, source)) ||
	    (source_other != NULL && format_compatible (format, source_other)))
		return format;

	format_reject (self, key, format);
	if (source != NULL && format_parse (source, &unused))
		return source;
	return NULL;
}

/*
 * format_with:
 * g_strdup_vprintf() of a format that checked_format() accepted.  It is
 * never a string literal (it comes from a locale file), which clang's
 * -Wformat-nonliteral reports even for a va_list call (gcc exempts those);
 * the check above is what makes it safe, so the warning is turned off for
 * this one call and nowhere else.
 */
static gchar *
format_with (const gchar *format,
             va_list      args)
{
	gchar *result;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
	result = g_strdup_vprintf (format, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
	return result;
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

    format = checked_format (self, key, FALSE, 0);
    if (format == NULL)
        return NULL;

    va_start (args, key);
    result = format_with (format, args);
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

    format = checked_format (self, key, TRUE, count);
    if (format == NULL)
        return NULL;

    va_start (args, count);
    result = format_with (format, args);
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
