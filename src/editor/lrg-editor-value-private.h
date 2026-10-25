/* lrg-editor-value-private.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private GValue helpers shared by the editor authoring types (param bags of
 * #LrgNodeVisual and #LrgComponentDesc).
 */

#pragma once

#include <glib-object.h>
#include <string.h>

/*
 * lrg_editor_value_equal:
 *
 * Compares two GValues for the purposes of change detection. Values of
 * different GTypes are unequal. Fundamental scalar types are compared
 * directly; strings with strcmp; anything else falls back to comparing the
 * g_strdup_value_contents() renderings, which is exact for enums/flags and
 * good enough for boxed vectors.
 */
static inline gboolean
lrg_editor_value_equal (const GValue *a,
                        const GValue *b)
{
	GType type;

	if (a == b)
		return TRUE;
	if (a == NULL || b == NULL)
		return FALSE;

	type = G_VALUE_TYPE (a);
	if (type != G_VALUE_TYPE (b))
		return FALSE;

	switch (G_TYPE_FUNDAMENTAL (type))
	{
	case G_TYPE_BOOLEAN:
		return g_value_get_boolean (a) == g_value_get_boolean (b);
	case G_TYPE_INT:
		return g_value_get_int (a) == g_value_get_int (b);
	case G_TYPE_UINT:
		return g_value_get_uint (a) == g_value_get_uint (b);
	case G_TYPE_INT64:
		return g_value_get_int64 (a) == g_value_get_int64 (b);
	case G_TYPE_UINT64:
		return g_value_get_uint64 (a) == g_value_get_uint64 (b);
	case G_TYPE_LONG:
		return g_value_get_long (a) == g_value_get_long (b);
	case G_TYPE_ULONG:
		return g_value_get_ulong (a) == g_value_get_ulong (b);
	case G_TYPE_FLOAT:
		return g_value_get_float (a) == g_value_get_float (b);
	case G_TYPE_DOUBLE:
		return g_value_get_double (a) == g_value_get_double (b);
	case G_TYPE_STRING:
		return g_strcmp0 (g_value_get_string (a), g_value_get_string (b)) == 0;
	case G_TYPE_ENUM:
		return g_value_get_enum (a) == g_value_get_enum (b);
	case G_TYPE_FLAGS:
		return g_value_get_flags (a) == g_value_get_flags (b);
	case G_TYPE_OBJECT:
		return g_value_get_object (a) == g_value_get_object (b);
	default:
		{
			g_autofree gchar *sa = g_strdup_value_contents (a);
			g_autofree gchar *sb = g_strdup_value_contents (b);

			return g_strcmp0 (sa, sb) == 0;
		}
	}
}

/*
 * lrg_editor_param_tables_equal:
 *
 * Compares two `gchar* -> GValue*` hash tables (the param bags) for equal
 * key sets and equal values.
 */
static inline gboolean
lrg_editor_param_tables_equal (GHashTable *a,
                               GHashTable *b)
{
	GHashTableIter iter;
	gpointer       key;
	gpointer       value;

	if (g_hash_table_size (a) != g_hash_table_size (b))
		return FALSE;

	g_hash_table_iter_init (&iter, a);
	while (g_hash_table_iter_next (&iter, &key, &value))
	{
		const GValue *other = g_hash_table_lookup (b, key);

		if (other == NULL || !lrg_editor_value_equal (value, other))
			return FALSE;
	}

	return TRUE;
}
