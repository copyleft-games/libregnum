/* lrg-inspector-nested.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-inspector-nested.h"

#include <gio/gio.h>
#include <string.h>

/* A new reference with variant wrappers removed. */
static GVariant *
peel (GVariant *cur)
{
	GVariant *held;

	held = g_variant_ref (cur);
	while (g_variant_is_of_type (held, G_VARIANT_TYPE_VARIANT))
	{
		GVariant *inner = g_variant_get_variant (held);

		g_variant_unref (held);
		held = inner;
	}
	return held;
}

static gboolean
parse_index (const gchar  *token,
             guint64      *out,
             GError      **error)
{
	gchar   *end = NULL;
	guint64  idx;

	if (token == NULL || token[0] == '\0')
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "empty path token");
		return FALSE;
	}
	idx = g_ascii_strtoull (token, &end, 10);
	if (end == token || *end != '\0')
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "not an index: %s", token);
		return FALSE;
	}
	*out = idx;
	return TRUE;
}

static GVariant *
step_get (GVariant     *cur,
          const gchar  *token,
          GError      **error)
{
	g_autoptr(GVariant) peeled = NULL;
	GVariant           *child = NULL;
	guint64             idx;
	gsize               n;

	peeled = peel (cur);
	if (g_variant_is_of_type (peeled, G_VARIANT_TYPE_VARDICT))
	{
		child = g_variant_lookup_value (peeled, token, NULL);
		if (child == NULL)
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "missing key %s", token);
		return child;
	}
	if (g_variant_is_of_type (peeled, G_VARIANT_TYPE ("av")))
	{
		if (!parse_index (token, &idx, error))
			return NULL;
		n = g_variant_n_children (peeled);
		if (idx >= n)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "index %" G_GUINT64_FORMAT " out of range", idx);
			return NULL;
		}
		return g_variant_get_child_value (peeled, (gsize) idx);
	}

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "cannot index a %s", g_variant_get_type_string (peeled));
	return NULL;
}

GVariant *
lrg_inspector_nested_get (GVariant     *root,
                          const gchar  *path,
                          GError      **error)
{
	g_auto(GStrv) tokens = NULL;
	GVariant     *cur = NULL;
	guint         i;

	g_return_val_if_fail (root != NULL, NULL);
	g_return_val_if_fail (path != NULL, NULL);

	tokens = g_strsplit (path, "/", -1);
	cur = g_variant_ref (root);
	for (i = 0; tokens[i] != NULL; i++)
	{
		GVariant *next;

		if (tokens[i][0] == '\0')
			continue;
		next = step_get (cur, tokens[i], error);
		g_variant_unref (cur);
		if (next == NULL)
			return NULL;
		cur = next;
	}

	{
		GVariant *leaf = peel (cur);

		g_variant_unref (cur);
		return leaf;
	}
}

static GVariant * set_at (GVariant *node, gchar **tokens, guint index, GVariant *value, GError **error);

static GVariant *
held_value (GVariant *value)
{
	return g_variant_ref_sink (value);
}

static GVariant *
set_dict (GVariant  *node,
          gchar    **tokens,
          guint      index,
          gboolean   last,
          GVariant  *value,
          GError   **error)
{
	const gchar    *key = tokens[index];
	GVariant       *child;
	GVariant       *replacement = NULL;
	GVariantBuilder b;
	GVariantIter    iter;
	const gchar    *k;
	GVariant       *val;
	gboolean        found = FALSE;

	child = g_variant_lookup_value (node, key, NULL);
	if (last)
	{
		if (child != NULL)
			g_variant_unref (child);
		replacement = held_value (value);
	}
	else
	{
		if (child == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "missing key %s", key);
			return NULL;
		}
		replacement = set_at (child, tokens, index + 1, value, error);
		g_variant_unref (child);
		if (replacement == NULL)
			return NULL;
	}

	g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
	g_variant_iter_init (&iter, node);
	while (g_variant_iter_next (&iter, "{&sv}", &k, &val))
	{
		if (g_strcmp0 (k, key) == 0)
		{
			g_variant_builder_add (&b, "{sv}", key, replacement);
			found = TRUE;
		}
		else
			g_variant_builder_add (&b, "{sv}", k, val);
		g_variant_unref (val);
	}
	if (!found)
		g_variant_builder_add (&b, "{sv}", key, replacement);
	g_variant_unref (replacement);
	/* builder_end() is floating. Callers unref, so hand back a full ref. */
	return g_variant_take_ref (g_variant_builder_end (&b));
}

static GVariant *
set_array (GVariant  *node,
           gchar    **tokens,
           guint      index,
           gboolean   last,
           GVariant  *value,
           GError   **error)
{
	guint64         idx;
	gsize           n;
	gsize           i;
	GVariant       *child;
	GVariant       *replacement = NULL;
	GVariantBuilder b;

	if (!parse_index (tokens[index], &idx, error))
		return NULL;
	n = g_variant_n_children (node);
	if (idx > n || (idx == n && !last))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "index %" G_GUINT64_FORMAT " out of range", idx);
		return NULL;
	}

	if (idx == n || last)
	{
		GVariant *sunk = held_value (value);

		if (g_variant_is_of_type (sunk, G_VARIANT_TYPE_VARIANT))
			replacement = sunk;
		else
		{
			replacement = g_variant_take_ref (g_variant_new_variant (sunk));
			g_variant_unref (sunk);
		}
	}
	else
	{
		child = g_variant_get_child_value (node, (gsize) idx);
		replacement = set_at (child, tokens, index + 1, value, error);
		g_variant_unref (child);
		if (replacement == NULL)
			return NULL;
	}

	g_variant_builder_init (&b, G_VARIANT_TYPE ("av"));
	for (i = 0; i < n; i++)
	{
		GVariant *elem = g_variant_get_child_value (node, i);

		if ((guint64) i == idx)
			g_variant_builder_add_value (&b, replacement);
		else
			g_variant_builder_add_value (&b, elem);
		g_variant_unref (elem);
	}
	if (idx == n)
		g_variant_builder_add_value (&b, replacement);
	g_variant_unref (replacement);
	return g_variant_take_ref (g_variant_builder_end (&b));
}

static GVariant *
set_at (GVariant  *node,
        gchar    **tokens,
        guint      index,
        GVariant  *value,
        GError   **error)
{
	GVariant *inner;
	GVariant *next;
	gboolean  last;

	if (node == NULL)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "missing value");
		return NULL;
	}

	/* A wrapper does not consume a path token. */
	if (g_variant_is_of_type (node, G_VARIANT_TYPE_VARIANT))
	{
		inner = g_variant_get_variant (node);
		next = set_at (inner, tokens, index, value, error);
		g_variant_unref (inner);
		if (next == NULL)
			return NULL;
		inner = g_variant_take_ref (g_variant_new_variant (next));
		g_variant_unref (next);
		return inner;
	}

	if (tokens[index] == NULL || tokens[index][0] == '\0')
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "empty path");
		return NULL;
	}

	last = (tokens[index + 1] == NULL || tokens[index + 1][0] == '\0');
	if (g_variant_is_of_type (node, G_VARIANT_TYPE_VARDICT))
		return set_dict (node, tokens, index, last, value, error);
	if (g_variant_is_of_type (node, G_VARIANT_TYPE ("av")))
		return set_array (node, tokens, index, last, value, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
	             "cannot index a %s", g_variant_get_type_string (node));
	return NULL;
}

GVariant *
lrg_inspector_nested_set (GVariant     *root,
                          const gchar  *path,
                          GVariant     *value,
                          GError      **error)
{
	g_auto(GStrv) tokens = NULL;

	g_return_val_if_fail (root != NULL, NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (value != NULL, NULL);

	tokens = g_strsplit (path, "/", -1);
	return set_at (root, tokens, 0, value, error);
}
