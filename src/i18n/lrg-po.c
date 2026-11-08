/* lrg-po.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-po.h"

#include <string.h>

G_DEFINE_QUARK (lrg-po-error-quark, lrg_po_error)

/* msgstr[n] indexes used by import and documented on lrg_po_import_file(). */
static const LrgPluralForm plural_index[] = {
	LRG_PLURAL_ONE,
	LRG_PLURAL_OTHER,
	LRG_PLURAL_FEW,
	LRG_PLURAL_MANY,
	LRG_PLURAL_TWO,
	LRG_PLURAL_ZERO
};

static gchar *
unescape (const gchar *body,
          gsize        len)
{
	GString *out;
	gsize    i;

	out = g_string_sized_new ((gssize) len);
	for (i = 0; i < len; i++)
	{
		if (body[i] == '\\' && i + 1 < len)
		{
			i++;
			if (body[i] == 'n')
				g_string_append_c (out, '\n');
			else if (body[i] == 't')
				g_string_append_c (out, '\t');
			else
				g_string_append_c (out, body[i]);
		}
		else
			g_string_append_c (out, body[i]);
	}
	return g_string_free (out, FALSE);
}

/* The quoted payload on a PO line, or NULL when the line has no quotes. */
static gchar *
quoted (const gchar *line)
{
	const gchar *open;
	const gchar *p;
	GString     *raw;

	open = strchr (line, '"');
	if (open == NULL)
		return NULL;
	raw = g_string_new (NULL);
	for (p = open + 1; *p != '\0'; p++)
	{
		if (*p == '\\' && p[1] != '\0')
		{
			g_string_append_c (raw, *p);
			p++;
			g_string_append_c (raw, *p);
			continue;
		}
		if (*p == '"')
			break;
		g_string_append_c (raw, *p);
	}
	return unescape (raw->str, raw->len);
}

static void
append_quoted (GString     **dest,
               const gchar  *text)
{
	if (*dest == NULL)
		*dest = g_string_new (text);
	else
		g_string_append (*dest, text);
}

typedef struct
{
	GString *msgid;
	GString *plural;
	GString *msgstr;
	GString *forms[6];
	gint     field;   /* 0 none, 1 msgid, 2 plural, 3 msgstr, 4+ form */
} Entry;

static void
entry_clear (Entry *entry)
{
	guint i;

	/* g_string_free takes a gboolean, so it is not a GDestroyNotify. */
	if (entry->msgid != NULL)
		g_string_free (entry->msgid, TRUE);
	entry->msgid = NULL;
	if (entry->plural != NULL)
		g_string_free (entry->plural, TRUE);
	entry->plural = NULL;
	if (entry->msgstr != NULL)
		g_string_free (entry->msgstr, TRUE);
	entry->msgstr = NULL;
	for (i = 0; i < 6; i++)
	{
		if (entry->forms[i] != NULL)
			g_string_free (entry->forms[i], TRUE);
		entry->forms[i] = NULL;
	}
	entry->field = 0;
}

static void
entry_flush (Entry     *entry,
             LrgLocale *locale,
             gint      *count)
{
	guint i;

	if (entry->msgid == NULL || entry->msgid->len == 0)
	{
		entry_clear (entry);
		return;
	}
	if (entry->msgstr != NULL)
		lrg_locale_set_string (locale, entry->msgid->str, entry->msgstr->str);
	for (i = 0; i < 6; i++)
	{
		if (entry->forms[i] != NULL)
			lrg_locale_set_plural (locale, entry->msgid->str,
			                       plural_index[i], entry->forms[i]->str);
	}
	if (entry->msgstr != NULL || entry->forms[0] != NULL)
		(*count)++;
	entry_clear (entry);
}

gint
lrg_po_import_file (LrgLocale    *locale,
                    const gchar  *path,
                    GError      **error)
{
	gchar  *text = NULL;
	gchar **lines = NULL;
	Entry   entry;
	gint    count = 0;
	guint   i;

	g_return_val_if_fail (LRG_IS_LOCALE (locale), -1);
	g_return_val_if_fail (path != NULL, -1);

	memset (&entry, 0, sizeof (entry));
	if (!g_file_get_contents (path, &text, NULL, error))
		return -1;
	lines = g_strsplit (text, "\n", -1);
	g_free (text);

	for (i = 0; lines[i] != NULL; i++)
	{
		const gchar *line = lines[i];
		gchar       *payload = NULL;
		gsize        n;

		n = strlen (line);
		if (n > 0 && line[n - 1] == '\r')
			((gchar *) line)[n - 1] = '\0';
		if (line[0] == '\0')
		{
			entry_flush (&entry, locale, &count);
			continue;
		}
		if (line[0] == '#')
			continue;
		payload = quoted (line);
		if (g_str_has_prefix (line, "msgid_plural"))
		{
			entry.field = 2;
			append_quoted (&entry.plural, payload != NULL ? payload : "");
		}
		else if (g_str_has_prefix (line, "msgid"))
		{
			entry_flush (&entry, locale, &count);
			entry.field = 1;
			append_quoted (&entry.msgid, payload != NULL ? payload : "");
		}
		else if (g_str_has_prefix (line, "msgstr["))
		{
			gint index = line[7] - '0';

			entry.field = 4 + index;
			if (index >= 0 && index < 6)
				append_quoted (&entry.forms[index], payload != NULL ? payload : "");
		}
		else if (g_str_has_prefix (line, "msgstr"))
		{
			entry.field = 3;
			append_quoted (&entry.msgstr, payload != NULL ? payload : "");
		}
		else if (line[0] == '"' && payload != NULL)
		{
			if (entry.field == 1)
				append_quoted (&entry.msgid, payload);
			else if (entry.field == 2)
				append_quoted (&entry.plural, payload);
			else if (entry.field == 3)
				append_quoted (&entry.msgstr, payload);
			else if (entry.field >= 4 && entry.field < 10)
				append_quoted (&entry.forms[entry.field - 4], payload);
		}
		g_free (payload);
	}
	entry_flush (&entry, locale, &count);
	g_strfreev (lines);
	return count;
}

static void
escape_to (GString     *out,
           const gchar *text)
{
	const gchar *p;

	g_string_append_c (out, '"');
	for (p = text; *p != '\0'; p++)
	{
		if (*p == '\\' || *p == '"')
		{
			g_string_append_c (out, '\\');
			g_string_append_c (out, *p);
		}
		else if (*p == '\n')
			g_string_append (out, "\\n");
		else
			g_string_append_c (out, *p);
	}
	g_string_append_c (out, '"');
}

gchar *
lrg_po_export (LrgLocale *locale)
{
	g_autoptr(GPtrArray) keys = NULL;
	GString             *out;
	guint                i;

	g_return_val_if_fail (LRG_IS_LOCALE (locale), NULL);

	out = g_string_new ("msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n");
	keys = lrg_locale_get_keys (locale);
	for (i = 0; keys != NULL && i < keys->len; i++)
	{
		const gchar *key = g_ptr_array_index (keys, i);
		const gchar *value = lrg_locale_get_string (locale, key);

		if (value == NULL)
			continue;
		g_string_append (out, "\nmsgid ");
		escape_to (out, key);
		g_string_append (out, "\nmsgstr ");
		escape_to (out, value);
		g_string_append_c (out, '\n');
	}
	return g_string_free (out, FALSE);
}

gboolean
lrg_po_export_file (LrgLocale    *locale,
                    const gchar  *path,
                    GError      **error)
{
	g_autofree gchar *text = NULL;

	g_return_val_if_fail (LRG_IS_LOCALE (locale), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_po_export (locale);
	return g_file_set_contents (path, text, -1, error);
}

static gboolean
key_char (gchar c)
{
	return g_ascii_isalnum (c) || c == '_' || c == '.' || c == '-';
}

static void
add_unique (GPtrArray   *keys,
            const gchar *start,
            gsize        len)
{
	gchar *key;
	guint  i;

	if (len == 0)
		return;
	key = g_strndup (start, len);
	for (i = 0; i < keys->len; i++)
	{
		if (g_strcmp0 (g_ptr_array_index (keys, i), key) == 0)
		{
			g_free (key);
			return;
		}
	}
	g_ptr_array_add (keys, key);
}

GStrv
lrg_po_extract (const gchar *text)
{
	GPtrArray   *keys;
	const gchar *p;

	g_return_val_if_fail (text != NULL, NULL);

	keys = g_ptr_array_new_with_free_func (g_free);
	for (p = text; *p != '\0'; p++)
	{
		if (*p == '@' && (p == text || !key_char (p[-1])) && key_char (p[1]))
		{
			const gchar *start = p + 1;
			gsize        len = 0;

			while (key_char (start[len]))
				len++;
			add_unique (keys, start, len);
			p = start + len - 1;
		}
		else if (g_str_has_prefix (p, "tr(\"") || g_str_has_prefix (p, "tr('") ||
		         g_str_has_prefix (p, "_(\"") || g_str_has_prefix (p, "_('"))
		{
			const gchar *start;
			gchar        quote;
			gsize        len = 0;

			if (p[0] == '_')
			{
				quote = p[2];
				start = p + 3;
			}
			else
			{
				quote = p[3];
				start = p + 4;
			}
			while (start[len] != '\0' && start[len] != quote)
				len++;
			add_unique (keys, start, len);
		}
	}
	g_ptr_array_add (keys, NULL);
	return (GStrv) g_ptr_array_free (keys, FALSE);
}

static const gchar *
accent (gchar c)
{
	switch (c)
	{
	case 'a': return "à";
	case 'e': return "é";
	case 'i': return "í";
	case 'o': return "ó";
	case 'u': return "ú";
	case 'A': return "À";
	case 'E': return "É";
	case 'I': return "Í";
	case 'O': return "Ó";
	case 'U': return "Ú";
	default:  return NULL;
	}
}

static gchar *
pseudo (const gchar *text)
{
	GString     *out;
	const gchar *p;

	out = g_string_new ("[");
	for (p = text; *p != '\0'; p++)
	{
		const gchar *rep = accent (*p);

		if (rep != NULL)
		{
			g_string_append (out, rep);
			g_string_append (out, rep);
		}
		else
			g_string_append_c (out, *p);
	}
	g_string_append_c (out, ']');
	return g_string_free (out, FALSE);
}

LrgLocale *
lrg_po_pseudolocalize (LrgLocale *source)
{
	g_autoptr(GPtrArray) keys = NULL;
	LrgLocale           *out;
	guint                i;

	g_return_val_if_fail (LRG_IS_LOCALE (source), NULL);

	out = lrg_locale_new ("qps-ploc", "Pseudolocale");
	keys = lrg_locale_get_keys (source);
	for (i = 0; keys != NULL && i < keys->len; i++)
	{
		const gchar      *key = g_ptr_array_index (keys, i);
		const gchar      *value = lrg_locale_get_string (source, key);
		g_autofree gchar *wrapped = NULL;

		if (value == NULL)
			continue;
		wrapped = pseudo (value);
		lrg_locale_set_string (out, key, wrapped);
	}
	return out;
}
