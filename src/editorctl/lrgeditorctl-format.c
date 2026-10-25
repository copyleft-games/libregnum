/* lrgeditorctl-format.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Formatting for lrgeditorctl. YAML goes through yaml-glib (JSON <-> YAML
 * conversion both ways), so what `get -o yaml` prints is exactly what
 * `edit` and `apply -f` read back.
 */

#include "lrgeditorctl-format.h"
#include "lrgeditorctl-client.h"
#include <yaml-glib.h>
#include <stdio.h>
#include <string.h>

gboolean
lrg_ctl_parse_format (const gchar  *text,
                      LrgCtlFormat *format)
{
	static const struct { const gchar *name; LrgCtlFormat format; } names[] = {
		{ "yaml", LRG_CTL_FORMAT_YAML }, { "yml", LRG_CTL_FORMAT_YAML },
		{ "json", LRG_CTL_FORMAT_JSON }, { "table", LRG_CTL_FORMAT_TABLE },
		{ "wide", LRG_CTL_FORMAT_WIDE }, { "name", LRG_CTL_FORMAT_NAME }
	};
	gsize i;

	for (i = 0; i < G_N_ELEMENTS (names); i++)
		if (g_ascii_strcasecmp (text, names[i].name) == 0)
		{
			*format = names[i].format;
			return TRUE;
		}
	return FALSE;
}

gchar *
lrg_ctl_to_json (JsonNode *node,
                 gboolean  pretty)
{
	g_autoptr(JsonGenerator) gen = json_generator_new ();

	json_generator_set_root (gen, node);
	json_generator_set_pretty (gen, pretty);
	json_generator_set_indent (gen, 2);
	return json_generator_to_data (gen, NULL);
}

gchar *
lrg_ctl_to_yaml (JsonNode *node)
{
	g_autoptr(YamlGenerator) gen = yaml_generator_new ();
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(GError)        error = NULL;
	gchar                   *out;

	/* Scalars at the top level read better without a document. */
	if (node == NULL || JSON_NODE_HOLDS_NULL (node))
		return g_strdup ("null\n");
	if (JSON_NODE_HOLDS_VALUE (node))
	{
		if (json_node_get_value_type (node) == G_TYPE_STRING)
			return g_strdup_printf ("%s\n", json_node_get_string (node));
		return lrg_ctl_to_json (node, FALSE);
	}

	root = yaml_node_from_json_node (node);
	yaml_generator_set_root (gen, root);
	yaml_generator_set_indent (gen, 2);
	yaml_generator_set_unicode (gen, TRUE);
	out = yaml_generator_to_data (gen, NULL, &error);
	return (out != NULL) ? out : lrg_ctl_to_json (node, TRUE);
}

void
lrg_ctl_print (JsonNode     *node,
               LrgCtlFormat  format)
{
	g_autofree gchar *text = NULL;

	if (format == LRG_CTL_FORMAT_JSON)
		text = lrg_ctl_to_json (node, TRUE);
	else
		text = lrg_ctl_to_yaml (node);

	fputs (text, stdout);
	if (*text != '\0' && text[strlen (text) - 1] != '\n')
		fputc ('\n', stdout);
}

/* A row member as display text; dots in @path descend into objects. */
gchar *
lrg_ctl_cell (JsonNode    *row,
              const gchar *path)
{
	g_auto(GStrv) parts = g_strsplit (path, ".", -1);
	JsonNode     *n = row;
	guint         i;

	for (i = 0; parts[i] != NULL; i++)
	{
		JsonObject *o;

		if (n == NULL || !JSON_NODE_HOLDS_OBJECT (n))
			return g_strdup ("");
		o = json_node_get_object (n);
		if (!json_object_has_member (o, parts[i]))
			return g_strdup ("");
		n = json_object_get_member (o, parts[i]);
	}

	if (n == NULL || JSON_NODE_HOLDS_NULL (n))
		return g_strdup ("-");
	if (JSON_NODE_HOLDS_ARRAY (n))
	{
		JsonArray *a = json_node_get_array (n);
		GString   *s = g_string_new (NULL);
		guint      k;

		/* Short numeric arrays (vectors) inline; otherwise a count. */
		if (json_array_get_length (a) <= 4)
		{
			for (k = 0; k < json_array_get_length (a); k++)
			{
				JsonNode *e = json_array_get_element (a, k);

				if (k > 0)
					g_string_append_c (s, ',');
				if (JSON_NODE_HOLDS_VALUE (e) && json_node_get_value_type (e) == G_TYPE_DOUBLE)
					g_string_append_printf (s, "%g", json_node_get_double (e));
				else if (JSON_NODE_HOLDS_VALUE (e) && json_node_get_value_type (e) == G_TYPE_STRING)
					g_string_append (s, json_node_get_string (e));
				else if (JSON_NODE_HOLDS_VALUE (e))
					g_string_append_printf (s, "%" G_GINT64_FORMAT, json_node_get_int (e));
			}
			return g_string_free (s, FALSE);
		}
		g_string_free (s, TRUE);
		return g_strdup_printf ("[%u]", json_array_get_length (a));
	}
	if (JSON_NODE_HOLDS_OBJECT (n))
		return g_strdup ("{...}");

	switch (json_node_get_value_type (n))
	{
	case G_TYPE_STRING:  return g_strdup (json_node_get_string (n));
	case G_TYPE_BOOLEAN: return g_strdup (json_node_get_boolean (n) ? "true" : "false");
	case G_TYPE_DOUBLE:  return g_strdup_printf ("%g", json_node_get_double (n));
	default:             return g_strdup_printf ("%" G_GINT64_FORMAT, json_node_get_int (n));
	}
}

void
lrg_ctl_print_table (JsonArray          *rows,
                     const LrgCtlColumn *columns,
                     gsize               n_columns,
                     LrgCtlFormat        format,
                     const gchar        *name_path)
{
	g_autofree gsize *widths = NULL;
	guint             r;
	gsize             c, last;
	gboolean          wide = (format == LRG_CTL_FORMAT_WIDE);

	if (format == LRG_CTL_FORMAT_YAML || format == LRG_CTL_FORMAT_JSON)
	{
		g_autoptr(JsonNode) node = json_node_new (JSON_NODE_ARRAY);

		json_node_set_array (node, rows);
		lrg_ctl_print (node, format);
		return;
	}
	if (format == LRG_CTL_FORMAT_NAME)
	{
		for (r = 0; r < json_array_get_length (rows); r++)
		{
			g_autofree gchar *cell = lrg_ctl_cell (json_array_get_element (rows, r), name_path);

			printf ("%s\n", cell);
		}
		return;
	}

	/* Column widths over header and cells, then aligned rows. */
	widths = g_new0 (gsize, n_columns);
	for (c = 0; c < n_columns; c++)
		widths[c] = g_utf8_strlen (columns[c].header, -1);
	for (r = 0; r < json_array_get_length (rows); r++)
		for (c = 0; c < n_columns; c++)
		{
			g_autofree gchar *cell = lrg_ctl_cell (json_array_get_element (rows, r), columns[c].path);

			widths[c] = MAX (widths[c], (gsize) g_utf8_strlen (cell, -1));
		}

	/* The last shown column gets no padding (no trailing blanks). */
	last = 0;
	for (c = 0; c < n_columns; c++)
		if (wide || !columns[c].wide)
			last = c;
	for (c = 0; c < n_columns; c++)
		if (wide || !columns[c].wide)
		{
			if (c == last)
				printf ("%s", columns[c].header);
			else
				printf ("%-*s   ", (gint) widths[c], columns[c].header);
		}
	printf ("\n");
	for (r = 0; r < json_array_get_length (rows); r++)
	{
		for (c = 0; c < n_columns; c++)
		{
			g_autofree gchar *cell = NULL;
			glong             pad;

			if (!wide && columns[c].wide)
				continue;
			cell = lrg_ctl_cell (json_array_get_element (rows, r), columns[c].path);
			pad = (glong) widths[c] - g_utf8_strlen (cell, -1);
			if (c == last)
				printf ("%s", cell);
			else
				printf ("%s%*s   ", cell, (gint) MAX (pad, 0), "");
		}
		printf ("\n");
	}
}

/* One or more documents: YAML (several separated by ---), or JSON. */
GPtrArray *
lrg_ctl_parse_documents (const gchar  *text,
                         GError      **error)
{
	GPtrArray            *out = g_ptr_array_new_with_free_func ((GDestroyNotify) json_node_unref);
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	guint                 i, n;

	/* JSON is YAML, but json-glib keeps number types exact. */
	{
		g_autoptr(JsonParser) jp = json_parser_new ();
		const gchar          *p = text;

		while (g_ascii_isspace (*p))
			p++;
		if ((*p == '{' || *p == '[') && json_parser_load_from_data (jp, text, -1, NULL))
		{
			g_ptr_array_add (out, json_node_copy (json_parser_get_root (jp)));
			return out;
		}
	}

	if (!yaml_parser_load_from_data (parser, text, -1, error))
	{
		g_ptr_array_unref (out);
		return NULL;
	}
	n = yaml_parser_get_n_documents (parser);
	for (i = 0; i < n; i++)
	{
		YamlDocument *doc = yaml_parser_get_document (parser, i);
		YamlNode     *root = (doc != NULL) ? yaml_document_get_root (doc) : NULL;

		if (root != NULL)
			g_ptr_array_add (out, yaml_node_to_json_node (root));
	}
	return out;
}

/* A command-line value: JSON when it looks like JSON, booleans, numbers,
 * "1,2,3" vectors, "null", otherwise a string. */
JsonNode *
lrg_ctl_parse_value (const gchar *text)
{
	JsonNode *node;
	gchar    *end = NULL;
	gdouble   d;

	if (text[0] == '[' || text[0] == '{' || text[0] == '"')
	{
		g_autoptr(JsonParser) parser = json_parser_new ();

		if (json_parser_load_from_data (parser, text, -1, NULL))
			return json_node_copy (json_parser_get_root (parser));
	}
	if (g_strcmp0 (text, "true") == 0 || g_strcmp0 (text, "false") == 0)
	{
		node = json_node_new (JSON_NODE_VALUE);
		json_node_set_boolean (node, text[0] == 't');
		return node;
	}
	if (g_strcmp0 (text, "null") == 0)
		return json_node_new (JSON_NODE_NULL);

	/* Vectors: two or more comma-separated numbers. */
	if (strchr (text, ',') != NULL)
	{
		g_auto(GStrv) parts = g_strsplit (text, ",", -1);
		JsonArray    *arr = json_array_new ();
		gboolean      numeric = TRUE;
		guint         i;

		for (i = 0; parts[i] != NULL && numeric; i++)
		{
			d = g_ascii_strtod (g_strstrip (parts[i]), &end);
			numeric = (end != NULL && *end == '\0' && end != parts[i]);
			if (numeric)
				json_array_add_double_element (arr, d);
		}
		if (numeric)
		{
			node = json_node_new (JSON_NODE_ARRAY);
			json_node_take_array (node, arr);
			return node;
		}
		json_array_unref (arr);
	}

	d = g_ascii_strtod (text, &end);
	if (end != NULL && end != text && *end == '\0')
	{
		node = json_node_new (JSON_NODE_VALUE);
		if (strchr (text, '.') == NULL && strchr (text, 'e') == NULL && strchr (text, 'E') == NULL)
			json_node_set_int (node, g_ascii_strtoll (text, NULL, 10));
		else
			json_node_set_double (node, d);
		return node;
	}

	node = json_node_new (JSON_NODE_VALUE);
	json_node_set_string (node, text);
	return node;
}

/* key=value arguments into @into. */
gboolean
lrg_ctl_parse_assignments (gchar * const  *args,
                           JsonObject     *into,
                           GError        **error)
{
	guint i;

	for (i = 0; args != NULL && args[i] != NULL; i++)
	{
		const gchar      *eq = strchr (args[i], '=');
		g_autofree gchar *key = NULL;

		if (eq == NULL || eq == args[i])
		{
			g_set_error (error, LRG_CTL_ERROR, LRG_CTL_ERROR_USAGE,
			             "Expected key=value, got '%s'", args[i]);
			return FALSE;
		}
		key = g_strndup (args[i], eq - args[i]);
		json_object_set_member (into, key, lrg_ctl_parse_value (eq + 1));
	}
	return TRUE;
}
