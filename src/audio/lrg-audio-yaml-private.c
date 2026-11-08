/* lrg-audio-yaml-private.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Reading and writing helpers of the 0.10.0 audio formats (see
 * lrg-audio-yaml-private.h).
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <stdarg.h>

#include "lrg-audio-yaml-private.h"
#include "../core/lrg-data-loader.h"

gboolean
lrg_audio_yaml_fail (LrgAudioYamlCtx *ctx,
                     const gchar     *path,
                     const gchar     *format,
                     ...)
{
	g_autofree gchar *message = NULL;
	va_list           args;

	if (ctx->error == NULL || *ctx->error != NULL)
		return FALSE;
	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);
	if (path != NULL && *path != '\0')
		g_set_error (ctx->error, ctx->domain, ctx->code_value, "%s: %s: %s", ctx->source, path, message);
	else
		g_set_error (ctx->error, ctx->domain, ctx->code_value, "%s: %s", ctx->source, message);
	return FALSE;
}

YamlNode *
lrg_audio_yaml_parse (LrgAudioYamlCtx  *ctx,
                      const gchar      *data,
                      gssize            length,
                      YamlParser      **out_parser)
{
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	g_autoptr(GError)     local = NULL;
	YamlNode             *root;

	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (ctx->error, ctx->domain, ctx->code_parse, "%s: %s", ctx->source, local->message);
		return NULL;
	}
	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error (ctx->error, ctx->domain, ctx->code_parse, "%s: the document is empty", ctx->source);
		return NULL;
	}
	*out_parser = g_steal_pointer (&parser);
	return root;
}

YamlMapping *
lrg_audio_yaml_map (LrgAudioYamlCtx *ctx,
                    YamlNode        *node,
                    const gchar     *path)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		lrg_audio_yaml_fail (ctx, path, "expected a mapping");
		return NULL;
	}
	return yaml_node_get_mapping (node);
}

YamlSequence *
lrg_audio_yaml_seq (LrgAudioYamlCtx *ctx,
                    YamlNode        *node,
                    const gchar     *path)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
	{
		lrg_audio_yaml_fail (ctx, path, "expected a list");
		return NULL;
	}
	return yaml_node_get_sequence (node);
}

const gchar *
lrg_audio_yaml_scalar (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

gboolean
lrg_audio_yaml_parse_double (const gchar *text,
                             gdouble     *out)
{
	gchar  *end = NULL;
	gdouble v;

	if (text == NULL)
		return FALSE;
	while (g_ascii_isspace (*text))
		text++;
	if (*text == '\0')
		return FALSE;
	v = g_ascii_strtod (text, &end);
	if (end == text)
		return FALSE;
	while (g_ascii_isspace (*end))
		end++;
	if (*end != '\0' || !isfinite (v))
		return FALSE;
	*out = v;
	return TRUE;
}

gboolean
lrg_audio_yaml_number (LrgAudioYamlCtx *ctx,
                       YamlNode        *node,
                       const gchar     *path,
                       gdouble          min,
                       gdouble          max,
                       gdouble         *out)
{
	gdouble v;

	if (!lrg_audio_yaml_parse_double (lrg_audio_yaml_scalar (node), &v))
		return lrg_audio_yaml_fail (ctx, path, "expected a number");
	if (v < min || v > max)
		return lrg_audio_yaml_fail (ctx, path, "%g is out of range (%g .. %g)", v, min, max);
	*out = v;
	return TRUE;
}

gboolean
lrg_audio_yaml_int (LrgAudioYamlCtx *ctx,
                    YamlNode        *node,
                    const gchar     *path,
                    gint64           min,
                    gint64           max,
                    gint64          *out)
{
	const gchar *text = lrg_audio_yaml_scalar (node);
	gchar       *end = NULL;
	gint64       v;

	if (text == NULL || *text == '\0')
		return lrg_audio_yaml_fail (ctx, path, "expected an integer");
	v = g_ascii_strtoll (text, &end, 10);
	if (end == text || *end != '\0')
		return lrg_audio_yaml_fail (ctx, path, "expected an integer");
	if (v < min || v > max)
		return lrg_audio_yaml_fail (ctx, path, "%" G_GINT64_FORMAT " is out of range (%" G_GINT64_FORMAT
		                            " .. %" G_GINT64_FORMAT ")", v, min, max);
	*out = v;
	return TRUE;
}

gboolean
lrg_audio_yaml_bool (LrgAudioYamlCtx *ctx,
                     YamlNode        *node,
                     const gchar     *path,
                     gboolean        *out)
{
	const gchar *text = lrg_audio_yaml_scalar (node);

	if (text != NULL && (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "yes") == 0
	                     || g_strcmp0 (text, "1") == 0 || g_ascii_strcasecmp (text, "on") == 0))
		*out = TRUE;
	else if (text != NULL && (g_ascii_strcasecmp (text, "false") == 0 || g_ascii_strcasecmp (text, "no") == 0
	                          || g_strcmp0 (text, "0") == 0 || g_ascii_strcasecmp (text, "off") == 0))
		*out = FALSE;
	else
		return lrg_audio_yaml_fail (ctx, path, "expected true or false");
	return TRUE;
}

gboolean
lrg_audio_yaml_string (LrgAudioYamlCtx  *ctx,
                       YamlNode         *node,
                       const gchar      *path,
                       const gchar     **out)
{
	const gchar *text = lrg_audio_yaml_scalar (node);

	if (text == NULL)
		return lrg_audio_yaml_fail (ctx, path, "expected text");
	*out = text;
	return TRUE;
}

YamlNode *
lrg_audio_yaml_new_map (YamlMapping **out_map)
{
	YamlNode *node = yaml_node_new_mapping (NULL);

	*out_map = yaml_node_get_mapping (node);
	return node;
}

YamlNode *
lrg_audio_yaml_new_seq (YamlSequence **out_seq)
{
	YamlNode *node = yaml_node_new_sequence (NULL);

	*out_seq = yaml_node_get_sequence (node);
	return node;
}

void
lrg_audio_yaml_set (YamlMapping *map,
                    const gchar *key,
                    YamlNode    *node)
{
	yaml_mapping_set_member (map, key, node);
	yaml_node_unref (node);
}

void
lrg_audio_yaml_append (YamlSequence *seq,
                       YamlNode     *node)
{
	yaml_sequence_add_element (seq, node);
	yaml_node_unref (node);
}

/*
 * lrg_audio_yaml_format_number:
 * The shortest text that parses back to @value: a value that is exactly a
 * float (most synthesis parameters were floats once) is written with the
 * shortest text that gives that float, everything else with the shortest
 * text that gives the double.  Integers print without a fraction.
 */
gchar *
lrg_audio_yaml_format_number (gdouble value)
{
	gchar    buf[G_ASCII_DTOSTR_BUF_SIZE];
	gchar    format[8];
	gboolean single;
	gint     digits;

	if (!isfinite (value))
		return g_strdup ("0");
	if (value == floor (value) && fabs (value) < 1e15)
		return g_strdup_printf ("%.0f", value);
	single = (gdouble) (gfloat) value == value;
	for (digits = 1; digits <= 17; digits++)
	{
		gdouble back;

		g_snprintf (format, sizeof format, "%%.%dg", digits);
		g_ascii_formatd (buf, sizeof buf, format, value);
		back = g_ascii_strtod (buf, NULL);
		if (single ? (gfloat) back == (gfloat) value : back == value)
			return g_strdup (buf);
	}
	return g_strdup (g_ascii_dtostr (buf, sizeof buf, value));
}

YamlNode *
lrg_audio_yaml_num (gdouble value)
{
	g_autofree gchar *text = lrg_audio_yaml_format_number (value);

	return yaml_node_new_scalar (text, YAML_SCALAR_STYLE_PLAIN);
}

YamlNode *
lrg_audio_yaml_str (const gchar *text)
{
	return lrg_data_yaml_string (text);
}

YamlNode *
lrg_audio_yaml_int_node (gint64 value)
{
	return lrg_data_yaml_int (value);
}

YamlNode *
lrg_audio_yaml_bool_node (gboolean value)
{
	return lrg_data_yaml_boolean (value);
}

gchar *
lrg_audio_yaml_to_data (YamlNode  *root,
                        gsize     *length,
                        GError   **error)
{
	return lrg_data_yaml_to_data (root, length, error);
}
