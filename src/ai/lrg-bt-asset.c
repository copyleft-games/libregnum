/* lrg-bt-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBtAsset: the .rbt behaviour tree asset, its node model, the binding
 * registry and LrgBtRunner (a built tree plus its live debug state).
 *
 * The file is laid out as:
 *   1. enumerations, errors, status names
 *   2. the shared YAML / variant / script helpers (also used by
 *      lrg-logic-machine.c through the LRG_AI_PRIVATE block of the header)
 *   3. the binding registry (actions, conditions, custom node types)
 *   4. LrgBtAssetNode
 *   5. LrgBtAsset: parsing, writing, accessors
 *   6. building (validation, binding, subtree expansion)
 *   7. LrgBtRunner
 *
 * Parsing is strict about values (a wrong type, an unknown operator or
 * policy, a duplicate id or an undefined subtree fail with an error whose
 * message starts with the key path) and keeps unknown keys, which are
 * written back where they were found.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#define LRG_AI_PRIVATE
#include "lrg-bt-asset.h"
#include "lrg-bt-composite.h"
#include "lrg-bt-decorator.h"
#include "lrg-bt-leaf.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_AI
#include "lrg-log.h"

/* ==========================================================================
 * 1. Enumerations, errors, status names
 * ========================================================================== */

G_DEFINE_QUARK (lrg-bt-asset-error-quark, lrg_bt_asset_error)

GType
lrg_bt_asset_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_BT_ASSET_ERROR_PARSE, "LRG_BT_ASSET_ERROR_PARSE", "parse" },
			{ LRG_BT_ASSET_ERROR_VERSION, "LRG_BT_ASSET_ERROR_VERSION", "version" },
			{ LRG_BT_ASSET_ERROR_INVALID, "LRG_BT_ASSET_ERROR_INVALID", "invalid" },
			{ LRG_BT_ASSET_ERROR_REFERENCE, "LRG_BT_ASSET_ERROR_REFERENCE", "reference" },
			{ LRG_BT_ASSET_ERROR_BINDING, "LRG_BT_ASSET_ERROR_BINDING", "binding" },
			{ LRG_BT_ASSET_ERROR_IO, "LRG_BT_ASSET_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgBtAssetError", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

GType
lrg_bt_node_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_BT_NODE_KIND_COMPOSITE, "LRG_BT_NODE_KIND_COMPOSITE", "composite" },
			{ LRG_BT_NODE_KIND_DECORATOR, "LRG_BT_NODE_KIND_DECORATOR", "decorator" },
			{ LRG_BT_NODE_KIND_LEAF, "LRG_BT_NODE_KIND_LEAF", "leaf" },
			{ LRG_BT_NODE_KIND_SUBTREE, "LRG_BT_NODE_KIND_SUBTREE", "subtree" },
			{ 0, NULL, NULL }
		};
		GType type = g_enum_register_static ("LrgBtNodeKind", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

const gchar *
lrg_bt_status_to_string (LrgBTStatus status)
{
	switch (status)
	{
	case LRG_BT_STATUS_SUCCESS: return "success";
	case LRG_BT_STATUS_FAILURE: return "failure";
	case LRG_BT_STATUS_RUNNING: return "running";
	case LRG_BT_STATUS_INVALID:
	default:                    return "invalid";
	}
}

/* error codes shared by the .rbt and .rfsm domains (same numbering) */
#define CODE_INVALID   (LRG_BT_ASSET_ERROR_INVALID)
#define CODE_REFERENCE (LRG_BT_ASSET_ERROR_REFERENCE)

/* ==========================================================================
 * 2. Shared helpers
 * ========================================================================== */

gboolean
lrg_ai_priv_fail (GError      **error,
                  GQuark        domain,
                  gint          code,
                  const gchar  *path,
                  const gchar  *fmt,
                  ...)
{
	g_autofree gchar *msg = NULL;
	va_list           ap;

	va_start (ap, fmt);
	msg = g_strdup_vprintf (fmt, ap);
	va_end (ap);
	if (path != NULL && *path != '\0')
		g_set_error (error, domain, code, "%s: %s", path, msg);
	else
		g_set_error_literal (error, domain, code, msg);
	return FALSE;
}

gchar *
lrg_ai_priv_sub_path (const gchar *path,
                      const gchar *key)
{
	return (path == NULL || *path == '\0') ? g_strdup (key) : g_strdup_printf ("%s.%s", path, key);
}

gchar *
lrg_ai_priv_index_path (const gchar *path,
                        const gchar *key,
                        guint        index)
{
	if (key == NULL)
		return g_strdup_printf ("%s[%u]", path != NULL ? path : "", index);
	return (path == NULL || *path == '\0') ? g_strdup_printf ("%s[%u]", key, index)
	                                       : g_strdup_printf ("%s.%s[%u]", path, key, index);
}

YamlMapping *
lrg_ai_priv_as_mapping (YamlNode     *node,
                        const gchar  *path,
                        GQuark        domain,
                        GError      **error)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		lrg_ai_priv_fail (error, domain, CODE_INVALID, path, "expected a mapping");
		return NULL;
	}
	return yaml_node_get_mapping (node);
}

YamlSequence *
lrg_ai_priv_as_sequence (YamlNode     *node,
                         const gchar  *path,
                         GQuark        domain,
                         GError      **error)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
	{
		lrg_ai_priv_fail (error, domain, CODE_INVALID, path, "expected a sequence");
		return NULL;
	}
	return yaml_node_get_sequence (node);
}

const gchar *
lrg_ai_priv_as_scalar (YamlNode     *node,
                       const gchar  *path,
                       GQuark        domain,
                       GError      **error)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR ||
	    yaml_node_get_scalar (node) == NULL)
	{
		lrg_ai_priv_fail (error, domain, CODE_INVALID, path, "expected a scalar");
		return NULL;
	}
	return yaml_node_get_scalar (node);
}

/* A member's scalar text; *out is NULL when the key is absent (an error
 * when @required). */
gboolean
lrg_ai_priv_get_string (YamlMapping  *map,
                        const gchar  *path,
                        const gchar  *key,
                        gboolean      required,
                        const gchar **out,
                        GQuark        domain,
                        GError      **error)
{
	g_autofree gchar *p = lrg_ai_priv_sub_path (path, key);
	YamlNode         *n = yaml_mapping_get_member (map, key);

	*out = NULL;
	if (n == NULL)
	{
		if (required)
			return lrg_ai_priv_fail (error, domain, CODE_INVALID, path, "missing '%s'", key);
		return TRUE;
	}
	*out = lrg_ai_priv_as_scalar (n, p, domain, error);
	return *out != NULL;
}

/*
 * parse_number_text:
 *
 * A decimal (or floating) number, the whole text, finite. Hex and other
 * C spellings are refused so "0x10" stays a string.
 */
static gboolean
parse_number_text (const gchar *text,
                   gdouble     *out)
{
	gchar  *end = NULL;
	gdouble v;

	if (text == NULL || *text == '\0' || strchr (text, 'x') != NULL || strchr (text, 'X') != NULL)
		return FALSE;
	v = g_ascii_strtod (text, &end);
	if (end == text || *end != '\0' || !isfinite (v))
		return FALSE;
	*out = v;
	return TRUE;
}

/* an integer spelling: optional sign, digits only */
static gboolean
parse_int_text (const gchar *text,
                gint64      *out)
{
	const gchar *p = text;
	gchar       *end = NULL;

	if (text == NULL)
		return FALSE;
	if (*p == '-' || *p == '+')
		p++;
	if (*p == '\0')
		return FALSE;
	for (; *p != '\0'; p++)
		if (!g_ascii_isdigit (*p))
			return FALSE;
	*out = g_ascii_strtoll (text, &end, 10);
	return end != NULL && *end == '\0';
}

gboolean
lrg_ai_priv_get_number (YamlMapping  *map,
                        const gchar  *path,
                        const gchar  *key,
                        gdouble      *out,
                        gboolean     *present,
                        GQuark        domain,
                        GError      **error)
{
	g_autofree gchar *p = lrg_ai_priv_sub_path (path, key);
	const gchar      *t;

	if (present != NULL)
		*present = FALSE;
	if (!lrg_ai_priv_get_string (map, path, key, FALSE, &t, domain, error))
		return FALSE;
	if (t == NULL)
		return TRUE;
	if (!parse_number_text (t, out))
		return lrg_ai_priv_fail (error, domain, CODE_INVALID, p, "'%s' is not a number", t);
	if (present != NULL)
		*present = TRUE;
	return TRUE;
}

/*
 * text_bool:
 *
 * The YAML core-schema boolean spellings.
 */
static gboolean
text_bool (const gchar *text,
           gboolean    *out)
{
	if (g_strcmp0 (text, "true") == 0 || g_strcmp0 (text, "True") == 0 || g_strcmp0 (text, "TRUE") == 0)
		*out = TRUE;
	else if (g_strcmp0 (text, "false") == 0 || g_strcmp0 (text, "False") == 0 || g_strcmp0 (text, "FALSE") == 0)
		*out = FALSE;
	else
		return FALSE;
	return TRUE;
}

/*
 * plain_text_to_variant:
 *
 * Types a plain scalar: a boolean, then an integer, then a number, else a
 * string. Returns a non-floating reference.
 */
static GVariant *
plain_text_to_variant (const gchar *text)
{
	gboolean b;
	gint64   i;
	gdouble  d;

	if (text_bool (text, &b))
		return g_variant_ref_sink (g_variant_new_boolean (b));
	if (parse_int_text (text, &i))
		return g_variant_ref_sink (g_variant_new_int64 (i));
	if (parse_number_text (text, &d))
		return g_variant_ref_sink (g_variant_new_double (d));
	return g_variant_ref_sink (g_variant_new_string (text));
}

GVariant *
lrg_ai_priv_scalar_to_variant (YamlNode *node)
{
	const gchar     *text = yaml_node_get_scalar (node);
	YamlScalarStyle  style = yaml_node_get_scalar_style (node);

	if (text == NULL)
		text = "";
	/* quoted and block scalars are strings, whatever they spell */
	if (style == YAML_SCALAR_STYLE_SINGLE_QUOTED || style == YAML_SCALAR_STYLE_DOUBLE_QUOTED ||
	    style == YAML_SCALAR_STYLE_LITERAL || style == YAML_SCALAR_STYLE_FOLDED)
		return g_variant_ref_sink (g_variant_new_string (text));
	return plain_text_to_variant (text);
}

YamlNode *
lrg_ai_priv_number_node (gdouble value)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE + 2];

	/* the shortest text that reads back as the same double, with a
	 * decimal point so it reads back as a double and not an integer */
	g_ascii_dtostr (buf, G_ASCII_DTOSTR_BUF_SIZE, value);
	if (strpbrk (buf, ".eEnN") == NULL)
		g_strlcat (buf, ".0", sizeof buf);
	return yaml_node_new_scalar (buf, YAML_SCALAR_STYLE_PLAIN);
}

YamlNode *
lrg_ai_priv_variant_to_node (GVariant *value)
{
	g_autoptr(GVariant) held = g_variant_ref_sink (value);   /* consumes a floating ref */
	const GVariantType *t = g_variant_get_type (value);
	g_autofree gchar   *text = NULL;

	if (g_variant_type_equal (t, G_VARIANT_TYPE_BOOLEAN))
		return yaml_node_new_scalar (g_variant_get_boolean (value) ? "true" : "false", YAML_SCALAR_STYLE_PLAIN);
	if (g_variant_type_equal (t, G_VARIANT_TYPE_DOUBLE))
		return lrg_ai_priv_number_node (g_variant_get_double (value));
	if (g_variant_type_equal (t, G_VARIANT_TYPE_INT64))
		text = g_strdup_printf ("%" G_GINT64_FORMAT, g_variant_get_int64 (value));
	else if (g_variant_type_equal (t, G_VARIANT_TYPE_INT32))
		text = g_strdup_printf ("%d", g_variant_get_int32 (value));
	else if (g_variant_type_equal (t, G_VARIANT_TYPE_UINT32))
		text = g_strdup_printf ("%u", g_variant_get_uint32 (value));
	else if (g_variant_type_equal (t, G_VARIANT_TYPE_UINT64))
		text = g_strdup_printf ("%" G_GUINT64_FORMAT, g_variant_get_uint64 (value));
	if (text != NULL)
		return yaml_node_new_scalar (text, YAML_SCALAR_STYLE_PLAIN);

	if (g_variant_type_equal (t, G_VARIANT_TYPE_STRING))
	{
		const gchar         *s = g_variant_get_string (value, NULL);
		g_autoptr(GVariant)  as_plain = plain_text_to_variant (s);
		gboolean             quote;

		/* quote what would read back as something else, or lose its
		 * edges */
		quote = !g_variant_is_of_type (as_plain, G_VARIANT_TYPE_STRING) || *s == '\0' ||
		        g_ascii_isspace (s[0]) || g_ascii_isspace (s[strlen (s) - 1]) ||
		        g_strcmp0 (s, "~") == 0 || g_ascii_strcasecmp (s, "null") == 0;
		return yaml_node_new_scalar (s, quote ? YAML_SCALAR_STYLE_DOUBLE_QUOTED : YAML_SCALAR_STYLE_ANY);
	}

	/* anything else: its text form, quoted */
	text = g_variant_print (value, FALSE);
	return yaml_node_new_scalar (text, YAML_SCALAR_STYLE_DOUBLE_QUOTED);
}

GVariant *
lrg_ai_priv_empty_dict (void)
{
	return g_variant_ref_sink (g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0));
}

/* one entry of an ordered dictionary */
typedef struct
{
	gchar    *key;
	GVariant *value;   /* non-floating */
} DictEntry;

static void
dict_entry_free (gpointer data)
{
	DictEntry *e = data;

	g_free (e->key);
	g_variant_unref (e->value);
	g_free (e);
}

GPtrArray *
lrg_ai_priv_dict_new (void)
{
	return g_ptr_array_new_with_free_func (dict_entry_free);
}

GPtrArray *
lrg_ai_priv_dict_copy (GPtrArray *dict)
{
	GPtrArray *copy = lrg_ai_priv_dict_new ();
	guint      i;

	for (i = 0; dict != NULL && i < dict->len; i++)
	{
		DictEntry *e = g_ptr_array_index (dict, i);

		lrg_ai_priv_dict_set (copy, e->key, e->value);
	}
	return copy;
}

void
lrg_ai_priv_dict_set (GPtrArray   *dict,
                      const gchar *key,
                      GVariant    *value)
{
	DictEntry *e;
	guint      i;

	/* replace in place (or drop) an existing key; append a new one: the
	 * order is the saved order */
	for (i = 0; i < dict->len; i++)
	{
		e = g_ptr_array_index (dict, i);
		if (g_strcmp0 (e->key, key) != 0)
			continue;
		if (value == NULL)
			g_ptr_array_remove_index (dict, i);
		else
		{
			g_variant_ref_sink (value);
			g_variant_unref (e->value);
			e->value = value;
		}
		return;
	}
	if (value == NULL)
		return;
	e = g_new0 (DictEntry, 1);
	e->key = g_strdup (key);
	e->value = g_variant_ref_sink (value);
	g_ptr_array_add (dict, e);
}

GVariant *
lrg_ai_priv_dict_lookup (GPtrArray   *dict,
                         const gchar *key)
{
	guint i;

	for (i = 0; dict != NULL && key != NULL && i < dict->len; i++)
	{
		DictEntry *e = g_ptr_array_index (dict, i);

		if (g_strcmp0 (e->key, key) == 0)
			return e->value;
	}
	return NULL;
}

gchar **
lrg_ai_priv_dict_keys (GPtrArray *dict)
{
	GPtrArray *out = g_ptr_array_new ();
	guint      i;

	for (i = 0; dict != NULL && i < dict->len; i++)
		g_ptr_array_add (out, g_strdup (((DictEntry *) g_ptr_array_index (dict, i))->key));
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

GVariant *
lrg_ai_priv_dict_to_variant (GPtrArray *dict)
{
	GVariantBuilder b;
	guint           i;

	g_variant_builder_init (&b, G_VARIANT_TYPE ("a{sv}"));
	for (i = 0; dict != NULL && i < dict->len; i++)
	{
		DictEntry *e = g_ptr_array_index (dict, i);

		g_variant_builder_add (&b, "{sv}", e->key, e->value);
	}
	return g_variant_ref_sink (g_variant_builder_end (&b));
}

YamlNode *
lrg_ai_priv_dict_to_node (GPtrArray *dict)
{
	YamlNode    *node = yaml_node_new_mapping (NULL);
	YamlMapping *map = yaml_node_get_mapping (node);
	guint        i;

	for (i = 0; dict != NULL && i < dict->len; i++)
	{
		DictEntry *e = g_ptr_array_index (dict, i);

		lrg_ai_priv_put (map, e->key, lrg_ai_priv_variant_to_node (e->value));
	}
	return node;
}

gboolean
lrg_ai_priv_parse_dict (YamlNode     *node,
                        const gchar  *path,
                        GPtrArray   **out,
                        GQuark        domain,
                        GError      **error)
{
	YamlMapping          *map;
	g_autoptr(GList)      keys = NULL;
	g_autoptr(GPtrArray)  dict = NULL;
	GList                *l;

	*out = NULL;
	map = lrg_ai_priv_as_mapping (node, path, domain, error);
	if (map == NULL)
		return FALSE;
	dict = lrg_ai_priv_dict_new ();
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		g_autofree gchar *p = lrg_ai_priv_sub_path (path, l->data);
		YamlNode         *v = yaml_mapping_get_member (map, l->data);
		GVariant         *value;

		if (lrg_ai_priv_as_scalar (v, p, domain, error) == NULL)
			return FALSE;
		value = lrg_ai_priv_scalar_to_variant (v);
		lrg_ai_priv_dict_set (dict, l->data, value);
		g_variant_unref (value);
	}
	*out = g_steal_pointer (&dict);
	return TRUE;
}

YamlMapping *
lrg_ai_priv_collect_extra (YamlMapping        *map,
                           const gchar *const *known)
{
	g_autoptr(GList) keys = yaml_mapping_get_members (map);
	YamlMapping     *extra = NULL;
	GList           *l;

	for (l = keys; l != NULL; l = l->next)
	{
		if (g_strv_contains (known, l->data))
			continue;
		if (extra == NULL)
			extra = yaml_mapping_new ();
		yaml_mapping_set_member (extra, l->data, yaml_mapping_get_member (map, l->data));
	}
	return extra;
}

void
lrg_ai_priv_copy_extra (YamlMapping *map,
                        YamlMapping *extra)
{
	g_autoptr(GList) keys = NULL;
	GList           *l;

	if (extra == NULL)
		return;
	keys = yaml_mapping_get_members (extra);
	for (l = keys; l != NULL; l = l->next)
		if (!yaml_mapping_has_member (map, l->data))
			yaml_mapping_set_member (map, l->data, yaml_mapping_get_member (extra, l->data));
}

void
lrg_ai_priv_put (YamlMapping *map,
                 const gchar *key,
                 YamlNode    *node)
{
	yaml_mapping_set_member (map, key, node);
	yaml_node_unref (node);
}

gboolean
lrg_ai_priv_parse_schema (LrgBlackboardSchema  *schema,
                          YamlNode             *node,
                          const gchar          *path,
                          GQuark                domain,
                          GError              **error)
{
	static const gchar * const known[] = { "name", "type", "default", NULL };
	YamlSequence *seq;
	guint         i;

	seq = lrg_ai_priv_as_sequence (node, path, domain, error);
	if (seq == NULL)
		return FALSE;
	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		g_autofree gchar    *ep = lrg_ai_priv_index_path (path, NULL, i);
		YamlMapping         *m = lrg_ai_priv_as_mapping (yaml_sequence_get_element (seq, i), ep, domain, error);
		const gchar         *name;
		const gchar         *type_text;
		YamlNode            *def_node;
		g_autoptr(GVariant)  def = NULL;
		g_autoptr(YamlMapping) extra = NULL;
		GType                type;

		if (m == NULL)
			return FALSE;
		if (!lrg_ai_priv_get_string (m, ep, "name", TRUE, &name, domain, error) ||
		    !lrg_ai_priv_get_string (m, ep, "type", TRUE, &type_text, domain, error))
			return FALSE;
		if (lrg_blackboard_schema_has_key (schema, name))
			return lrg_ai_priv_fail (error, domain, CODE_INVALID, ep, "duplicate blackboard key '%s'", name);
		type = lrg_blackboard_schema_type_from_string (type_text);
		if (type == G_TYPE_INVALID)
		{
			g_autofree gchar *tp = lrg_ai_priv_sub_path (ep, "type");

			return lrg_ai_priv_fail (error, domain, CODE_INVALID, tp,
			                         "unknown type '%s' (one of: int, float, bool, string, object)", type_text);
		}
		def_node = yaml_mapping_get_member (m, "default");
		if (def_node != NULL)
		{
			g_autofree gchar *dp = lrg_ai_priv_sub_path (ep, "default");

			if (type == G_TYPE_OBJECT)
				return lrg_ai_priv_fail (error, domain, CODE_INVALID, dp, "object keys have no default");
			if (lrg_ai_priv_as_scalar (def_node, dp, domain, error) == NULL)
				return FALSE;
			/* a string key takes the text as it is spelled */
			if (type == G_TYPE_STRING)
				def = g_variant_ref_sink (g_variant_new_string (yaml_node_get_scalar (def_node)));
			else
				def = lrg_ai_priv_scalar_to_variant (def_node);
			if (!lrg_blackboard_schema_add_key (schema, name, type, def))
				return lrg_ai_priv_fail (error, domain, CODE_INVALID, dp, "'%s' is not a %s",
				                         yaml_node_get_scalar (def_node), type_text);
		}
		else
			lrg_blackboard_schema_add_key (schema, name, type, NULL);
		extra = lrg_ai_priv_collect_extra (m, known);
		if (extra != NULL)
		{
			g_autoptr(GList) keys = yaml_mapping_get_members (extra);

			return lrg_ai_priv_fail (error, domain, CODE_INVALID, ep, "unknown blackboard key field '%s'",
			                         (const gchar *) keys->data);
		}
	}
	return TRUE;
}

YamlNode *
lrg_ai_priv_write_schema (LrgBlackboardSchema *schema)
{
	YamlNode     *node;
	YamlSequence *seq;
	guint         i;
	guint         n = lrg_blackboard_schema_get_key_count (schema);

	if (n == 0)
		return NULL;
	node = yaml_node_new_sequence (NULL);
	seq = yaml_node_get_sequence (node);
	for (i = 0; i < n; i++)
	{
		const gchar *name = lrg_blackboard_schema_get_key_name (schema, i);
		GType        type = lrg_blackboard_schema_get_key_type (schema, name);
		GVariant    *def = lrg_blackboard_schema_get_key_default (schema, name);
		YamlNode    *e = yaml_node_new_mapping (NULL);
		YamlMapping *m = yaml_node_get_mapping (e);

		yaml_mapping_set_string_member (m, "name", name);
		yaml_mapping_set_string_member (m, "type", lrg_blackboard_schema_type_to_string (type));
		if (def != NULL)
		{
			/* a string default is written as text the key type reads
			 * back; numbers and booleans as typed scalars */
			if (type == G_TYPE_STRING)
				lrg_ai_priv_put (m, "default", lrg_ai_priv_variant_to_node (def));
			else if (type == G_TYPE_INT)
			{
				g_autofree gchar *t = g_strdup_printf ("%d", g_variant_get_int32 (def));

				lrg_ai_priv_put (m, "default", yaml_node_new_scalar (t, YAML_SCALAR_STYLE_PLAIN));
			}
			else
				lrg_ai_priv_put (m, "default", lrg_ai_priv_variant_to_node (def));
		}
		yaml_sequence_add_element (seq, e);
		yaml_node_unref (e);
	}
	return node;
}

gboolean
lrg_ai_priv_parse_op (YamlMapping      *map,
                      const gchar      *path,
                      LrgBlackboardOp  *out_op,
                      GVariant        **out_value,
                      GQuark            domain,
                      GError          **error)
{
	const gchar *op_text;
	YamlNode    *vnode;

	*out_value = NULL;
	if (!lrg_ai_priv_get_string (map, path, "op", FALSE, &op_text, domain, error))
		return FALSE;
	vnode = yaml_mapping_get_member (map, "value");
	if (vnode != NULL)
	{
		g_autofree gchar *vp = lrg_ai_priv_sub_path (path, "value");

		if (lrg_ai_priv_as_scalar (vnode, vp, domain, error) == NULL)
			return FALSE;
		*out_value = lrg_ai_priv_scalar_to_variant (vnode);
	}
	if (op_text == NULL)
	{
		/* no operator: equality with a value, presence without one */
		*out_op = vnode != NULL ? LRG_BLACKBOARD_OP_EQ : LRG_BLACKBOARD_OP_SET;
		return TRUE;
	}
	if (!lrg_blackboard_op_from_string (op_text, out_op))
	{
		g_autofree gchar *op = lrg_ai_priv_sub_path (path, "op");

		g_clear_pointer (out_value, g_variant_unref);
		return lrg_ai_priv_fail (error, domain, CODE_INVALID, op,
		                         "unknown operator '%s' (one of: eq, ne, lt, le, gt, ge, set, unset)", op_text);
	}
	if (*out_op != LRG_BLACKBOARD_OP_SET && *out_op != LRG_BLACKBOARD_OP_UNSET && vnode == NULL)
		return lrg_ai_priv_fail (error, domain, CODE_INVALID, path, "operator '%s' needs a value", op_text);
	return TRUE;
}

gboolean
lrg_ai_priv_call_script (LrgScripting   *scripting,
                         const gchar    *func,
                         LrgBlackboard  *blackboard,
                         gfloat          delta_time,
                         GVariant       *params,
                         GValue         *ret,
                         GError        **error)
{
	g_autoptr(GArray) args = NULL;
	GValue            v = G_VALUE_INIT;
	GVariantIter      iter;
	const gchar      *k;
	GVariant         *p;
	gboolean          ok;
	guint             i;

	/* (blackboard, delta_time, param values in their saved order) */
	args = g_array_new (FALSE, TRUE, sizeof (GValue));
	g_value_init (&v, LRG_TYPE_BLACKBOARD);
	g_value_set_object (&v, blackboard);
	g_array_append_val (args, v);
	memset (&v, 0, sizeof v);
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, (gdouble) delta_time);
	g_array_append_val (args, v);
	if (params != NULL)
	{
		g_variant_iter_init (&iter, params);
		while (g_variant_iter_next (&iter, "{&sv}", &k, &p))
		{
			memset (&v, 0, sizeof v);
			if (g_variant_is_of_type (p, G_VARIANT_TYPE_BOOLEAN))
			{
				g_value_init (&v, G_TYPE_BOOLEAN);
				g_value_set_boolean (&v, g_variant_get_boolean (p));
			}
			else if (g_variant_is_of_type (p, G_VARIANT_TYPE_INT64))
			{
				g_value_init (&v, G_TYPE_INT64);
				g_value_set_int64 (&v, g_variant_get_int64 (p));
			}
			else if (g_variant_is_of_type (p, G_VARIANT_TYPE_DOUBLE))
			{
				g_value_init (&v, G_TYPE_DOUBLE);
				g_value_set_double (&v, g_variant_get_double (p));
			}
			else if (g_variant_is_of_type (p, G_VARIANT_TYPE_STRING))
			{
				g_value_init (&v, G_TYPE_STRING);
				g_value_set_string (&v, g_variant_get_string (p, NULL));
			}
			else
			{
				g_autofree gchar *t = g_variant_print (p, FALSE);

				g_value_init (&v, G_TYPE_STRING);
				g_value_take_string (&v, g_steal_pointer (&t));
			}
			g_array_append_val (args, v);
			g_variant_unref (p);
		}
	}

	ok = lrg_scripting_call_function (scripting, func, ret, args->len, (const GValue *) args->data, error);
	for (i = 0; i < args->len; i++)
		g_value_unset (&g_array_index (args, GValue, i));
	return ok;
}

/* a GValue's number, when it holds one */
static gboolean
value_number (const GValue *value,
              gdouble      *out)
{
	GValue d = G_VALUE_INIT;

	if (G_VALUE_TYPE (value) == G_TYPE_INVALID || G_VALUE_HOLDS_STRING (value) || G_VALUE_HOLDS_BOOLEAN (value))
		return FALSE;
	if (G_VALUE_HOLDS_ENUM (value))
	{
		*out = (gdouble) g_value_get_enum (value);
		return TRUE;
	}
	g_value_init (&d, G_TYPE_DOUBLE);
	if (!g_value_type_transformable (G_VALUE_TYPE (value), G_TYPE_DOUBLE) || !g_value_transform (value, &d))
		return FALSE;
	*out = g_value_get_double (&d);
	g_value_unset (&d);
	return TRUE;
}

LrgBTStatus
lrg_ai_priv_status_of_value (const GValue *value)
{
	gdouble d;

	/* nothing returned: done */
	if (value == NULL || G_VALUE_TYPE (value) == G_TYPE_INVALID || G_VALUE_TYPE (value) == G_TYPE_NONE)
		return LRG_BT_STATUS_SUCCESS;
	if (G_VALUE_HOLDS_BOOLEAN (value))
		return g_value_get_boolean (value) ? LRG_BT_STATUS_SUCCESS : LRG_BT_STATUS_FAILURE;
	if (G_VALUE_HOLDS_STRING (value))
	{
		const gchar *s = g_value_get_string (value);

		if (s != NULL && g_ascii_strcasecmp (s, "success") == 0)
			return LRG_BT_STATUS_SUCCESS;
		if (s != NULL && g_ascii_strcasecmp (s, "running") == 0)
			return LRG_BT_STATUS_RUNNING;
		return LRG_BT_STATUS_FAILURE;
	}
	if (value_number (value, &d))
	{
		/* an LrgBTStatus value: 1 success, 2 failure, 3 running */
		if (d == (gdouble) LRG_BT_STATUS_SUCCESS)
			return LRG_BT_STATUS_SUCCESS;
		if (d == (gdouble) LRG_BT_STATUS_RUNNING)
			return LRG_BT_STATUS_RUNNING;
		return LRG_BT_STATUS_FAILURE;
	}
	/* an object or anything else: present means done */
	return LRG_BT_STATUS_SUCCESS;
}

gboolean
lrg_ai_priv_truth_of_value (const GValue *value)
{
	gdouble d;

	if (value == NULL || G_VALUE_TYPE (value) == G_TYPE_INVALID || G_VALUE_TYPE (value) == G_TYPE_NONE)
		return FALSE;
	if (G_VALUE_HOLDS_BOOLEAN (value))
		return g_value_get_boolean (value);
	if (G_VALUE_HOLDS_STRING (value))
	{
		const gchar *s = g_value_get_string (value);

		return s != NULL && (g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "success") == 0);
	}
	if (value_number (value, &d))
		return d != 0.0;
	if (G_VALUE_HOLDS_OBJECT (value))
		return g_value_get_object (value) != NULL;
	return FALSE;
}

/* ==========================================================================
 * 3. The binding registry
 * ========================================================================== */

struct _LrgAiBound
{
	gint            ref;
	gpointer        func;
	gpointer        user_data;
	GDestroyNotify  destroy;
};

static GMutex      registry_lock;
static GHashTable *registry_actions;      /* name -> LrgAiBound */
static GHashTable *registry_conditions;   /* name -> LrgAiBound */
static GHashTable *registry_types;        /* name -> GType (as pointer) */

/* the built-in node types, in palette order */
typedef struct
{
	const gchar        *name;
	LrgBtNodeKind       kind;
	const gchar *const *props;
	gboolean            takes_params;
} Builtin;

static const gchar * const props_none[] = { NULL };
static const gchar * const props_parallel[] = { "policy", NULL };
static const gchar * const props_repeater[] = { "count", NULL };
static const gchar * const props_seconds[] = { "seconds", NULL };
static const gchar * const props_test[] = { "key", "op", "value", NULL };
static const gchar * const props_action[] = { "action", "script", NULL };
static const gchar * const props_condition[] = { "condition", "script", NULL };
static const gchar * const props_set[] = { "key", "value", NULL };
static const gchar * const props_wait[] = { "duration", NULL };
static const gchar * const props_subtree[] = { "ref", NULL };

static const Builtin builtins[] = {
	{ "sequence",   LRG_BT_NODE_KIND_COMPOSITE, props_none,      FALSE },
	{ "selector",   LRG_BT_NODE_KIND_COMPOSITE, props_none,      FALSE },
	{ "parallel",   LRG_BT_NODE_KIND_COMPOSITE, props_parallel,  FALSE },
	{ "inverter",   LRG_BT_NODE_KIND_DECORATOR, props_none,      FALSE },
	{ "repeater",   LRG_BT_NODE_KIND_DECORATOR, props_repeater,  FALSE },
	{ "succeeder",  LRG_BT_NODE_KIND_DECORATOR, props_none,      FALSE },
	{ "failer",     LRG_BT_NODE_KIND_DECORATOR, props_none,      FALSE },
	{ "cooldown",   LRG_BT_NODE_KIND_DECORATOR, props_seconds,   FALSE },
	{ "time_limit", LRG_BT_NODE_KIND_DECORATOR, props_seconds,   FALSE },
	{ "guard",      LRG_BT_NODE_KIND_DECORATOR, props_test,      FALSE },
	{ "action",     LRG_BT_NODE_KIND_LEAF,      props_action,    TRUE },
	{ "condition",  LRG_BT_NODE_KIND_LEAF,      props_condition, TRUE },
	{ "check",      LRG_BT_NODE_KIND_LEAF,      props_test,      FALSE },
	{ "set",        LRG_BT_NODE_KIND_LEAF,      props_set,       FALSE },
	{ "wait",       LRG_BT_NODE_KIND_LEAF,      props_wait,      FALSE },
	{ "subtree",    LRG_BT_NODE_KIND_SUBTREE,   props_subtree,   FALSE },
};

static const Builtin *
find_builtin (const gchar *name)
{
	guint i;

	for (i = 0; i < G_N_ELEMENTS (builtins); i++)
		if (g_strcmp0 (builtins[i].name, name) == 0)
			return &builtins[i];
	return NULL;
}

static LrgAiBound *
bound_ref (LrgAiBound *bound)
{
	g_atomic_int_inc (&bound->ref);
	return bound;
}

void
lrg_ai_priv_bound_unref (LrgAiBound *bound)
{
	if (bound == NULL)
		return;
	if (g_atomic_int_dec_and_test (&bound->ref))
	{
		if (bound->destroy != NULL)
			bound->destroy (bound->user_data);
		g_free (bound);
	}
}

/* the registry tables, created on first use (caller holds the lock) */
static void
registry_ensure (void)
{
	if (registry_actions != NULL)
		return;
	registry_actions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                          (GDestroyNotify) lrg_ai_priv_bound_unref);
	registry_conditions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
	                                             (GDestroyNotify) lrg_ai_priv_bound_unref);
	registry_types = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static void
register_bound (gboolean        action,
                const gchar    *name,
                gpointer        func,
                gpointer        user_data,
                GDestroyNotify  destroy)
{
	LrgAiBound *bound = g_new0 (LrgAiBound, 1);

	bound->ref = 1;
	bound->func = func;
	bound->user_data = user_data;
	bound->destroy = destroy;
	g_mutex_lock (&registry_lock);
	registry_ensure ();
	g_hash_table_replace (action ? registry_actions : registry_conditions, g_strdup (name), bound);
	g_mutex_unlock (&registry_lock);
}

void
lrg_bt_register_action (const gchar           *name,
                        LrgBtBoundActionFunc   func,
                        gpointer               user_data,
                        GDestroyNotify         destroy)
{
	g_return_if_fail (name != NULL && *name != '\0');
	g_return_if_fail (func != NULL);

	register_bound (TRUE, name, (gpointer) func, user_data, destroy);
}

void
lrg_bt_register_condition (const gchar             *name,
                           LrgBtBoundConditionFunc  func,
                           gpointer                 user_data,
                           GDestroyNotify           destroy)
{
	g_return_if_fail (name != NULL && *name != '\0');
	g_return_if_fail (func != NULL);

	register_bound (FALSE, name, (gpointer) func, user_data, destroy);
}

static gboolean
unregister_bound (gboolean     action,
                  const gchar *name)
{
	gboolean removed = FALSE;

	g_mutex_lock (&registry_lock);
	if (registry_actions != NULL)
		removed = g_hash_table_remove (action ? registry_actions : registry_conditions, name);
	g_mutex_unlock (&registry_lock);
	return removed;
}

gboolean
lrg_bt_unregister_action (const gchar *name)
{
	g_return_val_if_fail (name != NULL, FALSE);
	return unregister_bound (TRUE, name);
}

gboolean
lrg_bt_unregister_condition (const gchar *name)
{
	g_return_val_if_fail (name != NULL, FALSE);
	return unregister_bound (FALSE, name);
}

static LrgAiBound *
lookup_bound (gboolean     action,
              const gchar *name)
{
	LrgAiBound *bound = NULL;

	if (name == NULL)
		return NULL;
	g_mutex_lock (&registry_lock);
	if (registry_actions != NULL)
		bound = g_hash_table_lookup (action ? registry_actions : registry_conditions, name);
	if (bound != NULL)
		bound_ref (bound);
	g_mutex_unlock (&registry_lock);
	return bound;
}

LrgAiBound *
lrg_ai_priv_lookup_action (const gchar *name)
{
	return lookup_bound (TRUE, name);
}

LrgAiBound *
lrg_ai_priv_lookup_condition (const gchar *name)
{
	return lookup_bound (FALSE, name);
}

LrgBTStatus
lrg_ai_priv_bound_act (LrgAiBound    *bound,
                       LrgBlackboard *blackboard,
                       gfloat         delta_time,
                       GVariant      *params)
{
	LrgBtBoundActionFunc func = (LrgBtBoundActionFunc) bound->func;

	return func (blackboard, delta_time, params, bound->user_data);
}

gboolean
lrg_ai_priv_bound_test (LrgAiBound    *bound,
                        LrgBlackboard *blackboard,
                        GVariant      *params)
{
	LrgBtBoundConditionFunc func = (LrgBtBoundConditionFunc) bound->func;

	return func (blackboard, params, bound->user_data);
}

gboolean
lrg_bt_has_action (const gchar *name)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&registry_lock);

	return name != NULL && registry_actions != NULL && g_hash_table_contains (registry_actions, name);
}

gboolean
lrg_bt_has_condition (const gchar *name)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&registry_lock);

	return name != NULL && registry_conditions != NULL && g_hash_table_contains (registry_conditions, name);
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

/* a table's keys, sorted, as a strv (caller holds the lock) */
static gchar **
sorted_keys (GHashTable *table)
{
	GPtrArray     *out = g_ptr_array_new ();
	GHashTableIter iter;
	gpointer       key;

	if (table != NULL)
	{
		g_hash_table_iter_init (&iter, table);
		while (g_hash_table_iter_next (&iter, &key, NULL))
			g_ptr_array_add (out, g_strdup (key));
	}
	g_ptr_array_sort (out, compare_strings);
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

gchar **
lrg_bt_dup_action_names (void)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&registry_lock);

	return sorted_keys (registry_actions);
}

gchar **
lrg_bt_dup_condition_names (void)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&registry_lock);

	return sorted_keys (registry_conditions);
}

gboolean
lrg_bt_register_node_type (const gchar *name,
                           GType        type)
{
	g_return_val_if_fail (name != NULL && *name != '\0', FALSE);
	g_return_val_if_fail (g_type_is_a (type, LRG_TYPE_BT_NODE), FALSE);
	g_return_val_if_fail (!G_TYPE_IS_ABSTRACT (type), FALSE);

	if (find_builtin (name) != NULL)
		return FALSE;
	g_mutex_lock (&registry_lock);
	registry_ensure ();
	g_hash_table_replace (registry_types, g_strdup (name), GSIZE_TO_POINTER (type));
	g_mutex_unlock (&registry_lock);
	return TRUE;
}

/* a registered custom type, or 0 */
static GType
lookup_custom_type (const gchar *name)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&registry_lock);

	if (name == NULL || registry_types == NULL)
		return G_TYPE_INVALID;
	return (GType) GPOINTER_TO_SIZE (g_hash_table_lookup (registry_types, name));
}

static LrgBtNodeKind
kind_of_gtype (GType type)
{
	if (g_type_is_a (type, LRG_TYPE_BT_COMPOSITE))
		return LRG_BT_NODE_KIND_COMPOSITE;
	if (g_type_is_a (type, LRG_TYPE_BT_DECORATOR))
		return LRG_BT_NODE_KIND_DECORATOR;
	return LRG_BT_NODE_KIND_LEAF;
}

gboolean
lrg_bt_lookup_node_type (const gchar   *name,
                         LrgBtNodeKind *out_kind)
{
	const Builtin *b = find_builtin (name);
	GType          type;

	if (b != NULL)
	{
		if (out_kind != NULL)
			*out_kind = b->kind;
		return TRUE;
	}
	type = lookup_custom_type (name);
	if (type == G_TYPE_INVALID)
		return FALSE;
	if (out_kind != NULL)
		*out_kind = kind_of_gtype (type);
	return TRUE;
}

gchar **
lrg_bt_dup_node_type_names (void)
{
	GPtrArray *out = g_ptr_array_new ();
	g_auto(GStrv) custom = NULL;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (builtins); i++)
		g_ptr_array_add (out, g_strdup (builtins[i].name));
	g_mutex_lock (&registry_lock);
	custom = sorted_keys (registry_types);
	g_mutex_unlock (&registry_lock);
	for (i = 0; custom[i] != NULL; i++)
		g_ptr_array_add (out, g_strdup (custom[i]));
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

/* --- parameter helpers ------------------------------------------------- */

/* a parameter's value (a new reference), or NULL */
static GVariant *
params_lookup (GVariant    *params,
               const gchar *key)
{
	if (params == NULL || key == NULL || !g_variant_is_of_type (params, G_VARIANT_TYPE ("a{sv}")))
		return NULL;
	return g_variant_lookup_value (params, key, NULL);
}

gdouble
lrg_bt_params_get_double (GVariant    *params,
                          const gchar *key,
                          gdouble      default_value)
{
	g_autoptr(GVariant) v = params_lookup (params, key);

	if (v == NULL)
		return default_value;
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
		return g_variant_get_double (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT64))
		return (gdouble) g_variant_get_int64 (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT32))
		return (gdouble) g_variant_get_int32 (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN))
		return g_variant_get_boolean (v) ? 1.0 : 0.0;
	return default_value;
}

gint64
lrg_bt_params_get_int (GVariant    *params,
                       const gchar *key,
                       gint64       default_value)
{
	g_autoptr(GVariant) v = params_lookup (params, key);

	if (v == NULL)
		return default_value;
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT64))
		return g_variant_get_int64 (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT32))
		return (gint64) g_variant_get_int32 (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
		return (gint64) g_variant_get_double (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN))
		return g_variant_get_boolean (v) ? 1 : 0;
	return default_value;
}

gboolean
lrg_bt_params_get_bool (GVariant    *params,
                        const gchar *key,
                        gboolean     default_value)
{
	g_autoptr(GVariant) v = params_lookup (params, key);

	if (v == NULL)
		return default_value;
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN))
		return g_variant_get_boolean (v);
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT64))
		return g_variant_get_int64 (v) != 0;
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
		return g_variant_get_double (v) != 0.0;
	return default_value;
}

const gchar *
lrg_bt_params_get_string (GVariant    *params,
                          const gchar *key,
                          const gchar *default_value)
{
	g_autoptr(GVariant) v = params_lookup (params, key);

	if (v == NULL || !g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
		return default_value;
	/* the text lives in @params' data (or in the child @params holds), so
	 * it outlives this reference */
	return g_variant_get_string (v, NULL);
}

/* ==========================================================================
 * 4. LrgBtAssetNode
 * ========================================================================== */

/* how a node's children were written, so unknown types keep their shape */
typedef enum
{
	LOADED_NONE,
	LOADED_CHILD,
	LOADED_CHILDREN
} LoadedWith;

struct _LrgBtAssetNode
{
	GObject          parent_instance;

	gchar           *type_name;
	gchar           *id;
	gboolean         id_explicit;
	gchar           *name;
	GPtrArray       *props;      /* ordered dict, saved order */
	GPtrArray       *params;     /* ordered dict, saved order */
	GVariant        *params_v;   /* a{sv} cache of params, or NULL */
	GPtrArray       *children;   /* LrgBtAssetNode */
	LrgBtAssetNode  *parent;     /* not owned */
	LoadedWith       loaded_with;
	YamlMapping     *extra;      /* unknown keys, or NULL */
};

G_DEFINE_FINAL_TYPE (LrgBtAssetNode, lrg_bt_asset_node, G_TYPE_OBJECT)

static void
lrg_bt_asset_node_dispose (GObject *object)
{
	LrgBtAssetNode *self = LRG_BT_ASSET_NODE (object);
	guint           i;

	if (self->children != NULL)
	{
		for (i = 0; i < self->children->len; i++)
			((LrgBtAssetNode *) g_ptr_array_index (self->children, i))->parent = NULL;
		g_clear_pointer (&self->children, g_ptr_array_unref);
	}

	G_OBJECT_CLASS (lrg_bt_asset_node_parent_class)->dispose (object);
}

static void
lrg_bt_asset_node_finalize (GObject *object)
{
	LrgBtAssetNode *self = LRG_BT_ASSET_NODE (object);

	g_free (self->type_name);
	g_free (self->id);
	g_free (self->name);
	g_clear_pointer (&self->props, g_ptr_array_unref);
	g_clear_pointer (&self->params, g_ptr_array_unref);
	g_clear_pointer (&self->params_v, g_variant_unref);
	g_clear_pointer (&self->extra, yaml_mapping_unref);

	G_OBJECT_CLASS (lrg_bt_asset_node_parent_class)->finalize (object);
}

static void
lrg_bt_asset_node_class_init (LrgBtAssetNodeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_bt_asset_node_dispose;
	object_class->finalize = lrg_bt_asset_node_finalize;
}

static void
lrg_bt_asset_node_init (LrgBtAssetNode *self)
{
	self->children = g_ptr_array_new_with_free_func (g_object_unref);
	self->props = lrg_ai_priv_dict_new ();
	self->params = lrg_ai_priv_dict_new ();
}

LrgBtAssetNode *
lrg_bt_asset_node_new (const gchar *type_name)
{
	LrgBtAssetNode *self;

	g_return_val_if_fail (type_name != NULL && *type_name != '\0', NULL);

	self = g_object_new (LRG_TYPE_BT_ASSET_NODE, NULL);
	self->type_name = g_strdup (type_name);
	return self;
}

LrgBtAssetNode *
lrg_bt_asset_node_copy (LrgBtAssetNode *self)
{
	LrgBtAssetNode *copy;
	guint           i;

	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);

	copy = lrg_bt_asset_node_new (self->type_name);
	copy->id = g_strdup (self->id);
	copy->id_explicit = self->id_explicit;
	copy->name = g_strdup (self->name);
	g_ptr_array_unref (copy->props);
	copy->props = lrg_ai_priv_dict_copy (self->props);
	g_ptr_array_unref (copy->params);
	copy->params = lrg_ai_priv_dict_copy (self->params);
	copy->loaded_with = self->loaded_with;
	copy->extra = self->extra != NULL ? yaml_mapping_ref (self->extra) : NULL;
	for (i = 0; i < self->children->len; i++)
	{
		g_autoptr(LrgBtAssetNode) child = lrg_bt_asset_node_copy (g_ptr_array_index (self->children, i));

		lrg_bt_asset_node_insert_child (copy, child, -1);
	}
	return copy;
}

const gchar *
lrg_bt_asset_node_get_type_name (LrgBtAssetNode *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);
	return self->type_name;
}

void
lrg_bt_asset_node_set_type_name (LrgBtAssetNode *self,
                                 const gchar    *type_name)
{
	g_return_if_fail (LRG_IS_BT_ASSET_NODE (self));
	g_return_if_fail (type_name != NULL && *type_name != '\0');

	g_free (self->type_name);
	self->type_name = g_strdup (type_name);
}

LrgBtNodeKind
lrg_bt_asset_node_get_kind (LrgBtAssetNode *self)
{
	LrgBtNodeKind kind;

	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), LRG_BT_NODE_KIND_LEAF);

	if (lrg_bt_lookup_node_type (self->type_name, &kind))
		return kind;
	/* unknown type: guess from how it was written, then its children */
	if (self->loaded_with == LOADED_CHILDREN || self->children->len > 1)
		return LRG_BT_NODE_KIND_COMPOSITE;
	if (self->loaded_with == LOADED_CHILD || self->children->len == 1)
		return LRG_BT_NODE_KIND_DECORATOR;
	return LRG_BT_NODE_KIND_LEAF;
}

const gchar *
lrg_bt_asset_node_get_id (LrgBtAssetNode *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);
	return self->id;
}

void
lrg_bt_asset_node_set_id (LrgBtAssetNode *self,
                          const gchar    *id)
{
	g_return_if_fail (LRG_IS_BT_ASSET_NODE (self));

	g_free (self->id);
	self->id = g_strdup (id);
	self->id_explicit = id != NULL;
}

gboolean
lrg_bt_asset_node_has_explicit_id (LrgBtAssetNode *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), FALSE);
	return self->id_explicit;
}

const gchar *
lrg_bt_asset_node_get_name (LrgBtAssetNode *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);
	return self->name;
}

void
lrg_bt_asset_node_set_name (LrgBtAssetNode *self,
                            const gchar    *name)
{
	g_return_if_fail (LRG_IS_BT_ASSET_NODE (self));

	g_free (self->name);
	self->name = g_strdup (name);
}

GVariant *
lrg_bt_asset_node_get_prop (LrgBtAssetNode *self,
                            const gchar    *key)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);
	return lrg_ai_priv_dict_lookup (self->props, key);
}

void
lrg_bt_asset_node_set_prop (LrgBtAssetNode *self,
                            const gchar    *key,
                            GVariant       *value)
{
	g_return_if_fail (LRG_IS_BT_ASSET_NODE (self));
	g_return_if_fail (key != NULL);

	lrg_ai_priv_dict_set (self->props, key, value);
}

gchar **
lrg_bt_asset_node_dup_prop_names (LrgBtAssetNode *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);
	return lrg_ai_priv_dict_keys (self->props);
}

GVariant *
lrg_bt_asset_node_get_param (LrgBtAssetNode *self,
                             const gchar    *key)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);
	return lrg_ai_priv_dict_lookup (self->params, key);
}

void
lrg_bt_asset_node_set_param (LrgBtAssetNode *self,
                             const gchar    *key,
                             GVariant       *value)
{
	g_return_if_fail (LRG_IS_BT_ASSET_NODE (self));
	g_return_if_fail (key != NULL);

	lrg_ai_priv_dict_set (self->params, key, value);
	g_clear_pointer (&self->params_v, g_variant_unref);
}

gchar **
lrg_bt_asset_node_dup_param_names (LrgBtAssetNode *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);
	return lrg_ai_priv_dict_keys (self->params);
}

GVariant *
lrg_bt_asset_node_get_params (LrgBtAssetNode *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);

	if (self->params->len == 0)
		return NULL;
	if (self->params_v == NULL)
		self->params_v = lrg_ai_priv_dict_to_variant (self->params);
	return self->params_v;
}

guint
lrg_bt_asset_node_get_child_count (LrgBtAssetNode *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), 0);
	return self->children->len;
}

LrgBtAssetNode *
lrg_bt_asset_node_get_child (LrgBtAssetNode *self,
                             guint           index)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);

	if (index >= self->children->len)
		return NULL;
	return g_ptr_array_index (self->children, index);
}

void
lrg_bt_asset_node_insert_child (LrgBtAssetNode *self,
                                LrgBtAssetNode *child,
                                gint            position)
{
	g_return_if_fail (LRG_IS_BT_ASSET_NODE (self));
	g_return_if_fail (LRG_IS_BT_ASSET_NODE (child));
	g_return_if_fail (child->parent == NULL);
	g_return_if_fail (child != self);

	child->parent = self;
	if (position < 0 || (guint) position >= self->children->len)
		g_ptr_array_add (self->children, g_object_ref (child));
	else
		g_ptr_array_insert (self->children, position, g_object_ref (child));
}

gboolean
lrg_bt_asset_node_remove_child (LrgBtAssetNode *self,
                                LrgBtAssetNode *child)
{
	guint index;

	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), FALSE);
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (child), FALSE);

	if (!g_ptr_array_find (self->children, child, &index))
		return FALSE;
	child->parent = NULL;
	g_ptr_array_remove_index (self->children, index);
	return TRUE;
}

LrgBtAssetNode *
lrg_bt_asset_node_get_parent (LrgBtAssetNode *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET_NODE (self), NULL);
	return self->parent;
}

/* ==========================================================================
 * 5. LrgBtAsset
 * ========================================================================== */

/* a named subtree: in another file (path) or written inline (root) */
typedef struct
{
	gchar          *name;
	gchar          *path;
	LrgBtAssetNode *root;
	YamlMapping    *extra;
} Subtree;

struct _LrgBtAsset
{
	GObject              parent_instance;

	gchar               *name;
	gchar               *base_dir;
	LrgBlackboardSchema *schema;
	LrgBtAssetNode      *root;
	GPtrArray           *subtrees;   /* Subtree, declaration order */
	YamlMapping         *extra;
	gboolean             allow_unbound;
	GHashTable          *loaded;     /* resolved path -> LrgBtAsset (file subtrees) */
};

G_DEFINE_FINAL_TYPE (LrgBtAsset, lrg_bt_asset, G_TYPE_OBJECT)

static void
subtree_free (gpointer data)
{
	Subtree *st = data;

	g_free (st->name);
	g_free (st->path);
	g_clear_object (&st->root);
	g_clear_pointer (&st->extra, yaml_mapping_unref);
	g_free (st);
}

static void
lrg_bt_asset_finalize (GObject *object)
{
	LrgBtAsset *self = LRG_BT_ASSET (object);

	g_free (self->name);
	g_free (self->base_dir);
	g_clear_object (&self->schema);
	g_clear_object (&self->root);
	g_clear_pointer (&self->subtrees, g_ptr_array_unref);
	g_clear_pointer (&self->extra, yaml_mapping_unref);
	g_clear_pointer (&self->loaded, g_hash_table_unref);

	G_OBJECT_CLASS (lrg_bt_asset_parent_class)->finalize (object);
}

static void
lrg_bt_asset_class_init (LrgBtAssetClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_bt_asset_finalize;
}

static void
lrg_bt_asset_init (LrgBtAsset *self)
{
	self->schema = lrg_blackboard_schema_new ();
	self->subtrees = g_ptr_array_new_with_free_func (subtree_free);
	self->loaded = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

LrgBtAsset *
lrg_bt_asset_new (void)
{
	return g_object_new (LRG_TYPE_BT_ASSET, NULL);
}

/* --- parsing ----------------------------------------------------------- */

typedef struct
{
	GError     **error;
	GHashTable  *ids;        /* explicit ids seen (not owned strings) */
	GPtrArray   *refs;       /* subtree nodes, checked once all are known */
	GPtrArray   *ref_paths;  /* their key paths */
} Parse;

static gboolean
pfail (Parse       *ctx,
       gint         code,
       const gchar *path,
       const gchar *msg)
{
	return lrg_ai_priv_fail (ctx->error, LRG_BT_ASSET_ERROR, code, path, "%s", msg);
}

/* the expected shape of a built-in property */
typedef enum
{
	PROP_STRING,
	PROP_INT,
	PROP_NUMBER,
	PROP_ANY,
	PROP_POLICY,
	PROP_OP
} PropShape;

static PropShape
prop_shape (const gchar *key)
{
	if (g_strcmp0 (key, "count") == 0)
		return PROP_INT;
	if (g_strcmp0 (key, "seconds") == 0 || g_strcmp0 (key, "duration") == 0)
		return PROP_NUMBER;
	if (g_strcmp0 (key, "value") == 0)
		return PROP_ANY;
	if (g_strcmp0 (key, "policy") == 0)
		return PROP_POLICY;
	if (g_strcmp0 (key, "op") == 0)
		return PROP_OP;
	return PROP_STRING;
}

/*
 * parse_prop:
 *
 * Reads one built-in property from its scalar with the shape the key
 * calls for. Returns a non-floating variant or NULL with the error set.
 */
static GVariant *
parse_prop (Parse       *ctx,
            YamlNode    *node,
            const gchar *key,
            const gchar *path)
{
	const gchar     *text = lrg_ai_priv_as_scalar (node, path, LRG_BT_ASSET_ERROR, ctx->error);
	LrgBlackboardOp  op;
	gint64           i;
	gdouble          d;

	if (text == NULL)
		return NULL;
	switch (prop_shape (key))
	{
	case PROP_INT:
		if (!parse_int_text (text, &i) || i < 0)
		{
			g_autofree gchar *m = g_strdup_printf ("'%s' is not a non-negative integer", text);

			pfail (ctx, CODE_INVALID, path, m);
			return NULL;
		}
		return g_variant_ref_sink (g_variant_new_int64 (i));

	case PROP_NUMBER:
		if (!parse_number_text (text, &d) || d < 0.0)
		{
			g_autofree gchar *m = g_strdup_printf ("'%s' is not a non-negative number", text);

			pfail (ctx, CODE_INVALID, path, m);
			return NULL;
		}
		return g_variant_ref_sink (g_variant_new_double (d));

	case PROP_POLICY:
		if (g_strcmp0 (text, "one") != 0 && g_strcmp0 (text, "all") != 0)
		{
			g_autofree gchar *m = g_strdup_printf ("unknown policy '%s' (one of: one, all)", text);

			pfail (ctx, CODE_INVALID, path, m);
			return NULL;
		}
		return g_variant_ref_sink (g_variant_new_string (text));

	case PROP_OP:
		if (!lrg_blackboard_op_from_string (text, &op))
		{
			g_autofree gchar *m = g_strdup_printf ("unknown operator '%s' (one of: eq, ne, lt, le, gt, ge, set, unset)",
			                                       text);

			pfail (ctx, CODE_INVALID, path, m);
			return NULL;
		}
		/* stored by its canonical spelling */
		return g_variant_ref_sink (g_variant_new_string (lrg_blackboard_op_to_string (op)));

	case PROP_ANY:
		return lrg_ai_priv_scalar_to_variant (node);

	case PROP_STRING:
	default:
		return g_variant_ref_sink (g_variant_new_string (text));
	}
}

static LrgBtAssetNode *
parse_node (Parse       *ctx,
            YamlNode    *ynode,
            const gchar *path)
{
	static const gchar * const structural[] = { "type", "id", "name", "child", "children", "params", NULL };
	g_autoptr(LrgBtAssetNode) node = NULL;
	g_autoptr(GPtrArray)      known = NULL;
	g_autoptr(GList)          keys = NULL;
	YamlMapping              *map;
	const Builtin            *builtin;
	const gchar              *type;
	const gchar              *id;
	const gchar              *name;
	YamlNode                 *child;
	YamlNode                 *children;
	GList                    *l;
	guint                     i;

	map = lrg_ai_priv_as_mapping (ynode, path, LRG_BT_ASSET_ERROR, ctx->error);
	if (map == NULL)
		return NULL;
	if (!lrg_ai_priv_get_string (map, path, "type", TRUE, &type, LRG_BT_ASSET_ERROR, ctx->error) ||
	    !lrg_ai_priv_get_string (map, path, "id", FALSE, &id, LRG_BT_ASSET_ERROR, ctx->error) ||
	    !lrg_ai_priv_get_string (map, path, "name", FALSE, &name, LRG_BT_ASSET_ERROR, ctx->error))
		return NULL;

	node = lrg_bt_asset_node_new (type);
	if (id != NULL)
	{
		g_autofree gchar *ip = lrg_ai_priv_sub_path (path, "id");

		if (*id == '\0' || strchr (id, '/') != NULL)
		{
			pfail (ctx, CODE_INVALID, ip, "ids must be non-empty and contain no '/'");
			return NULL;
		}
		if (g_hash_table_contains (ctx->ids, id))
		{
			g_autofree gchar *m = g_strdup_printf ("duplicate id '%s'", id);

			pfail (ctx, CODE_INVALID, ip, m);
			return NULL;
		}
		lrg_bt_asset_node_set_id (node, id);
		g_hash_table_add (ctx->ids, node->id);
	}
	lrg_bt_asset_node_set_name (node, name);

	/* the type's properties: typed for built-ins, every other scalar key
	 * for custom and unknown types */
	known = g_ptr_array_new ();
	for (i = 0; structural[i] != NULL; i++)
		g_ptr_array_add (known, (gpointer) structural[i]);
	builtin = find_builtin (type);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar         *key = l->data;
		YamlNode            *v = yaml_mapping_get_member (map, key);
		g_autofree gchar    *kp = lrg_ai_priv_sub_path (path, key);
		g_autoptr(GVariant)  value = NULL;

		if (g_strv_contains (structural, key))
			continue;
		if (builtin != NULL)
		{
			if (!g_strv_contains (builtin->props, key))
				continue;     /* an unknown key: kept below */
			value = parse_prop (ctx, v, key, kp);
			if (value == NULL)
				return NULL;
		}
		else
		{
			if (yaml_node_get_node_type (v) != YAML_NODE_SCALAR || yaml_node_get_scalar (v) == NULL)
				continue;     /* not a property: kept as an unknown key */
			value = lrg_ai_priv_scalar_to_variant (v);
		}
		lrg_bt_asset_node_set_prop (node, key, value);
		g_ptr_array_add (known, (gpointer) key);
	}

	/* binding parameters */
	if (yaml_mapping_has_member (map, "params"))
	{
		g_autofree gchar *pp = lrg_ai_priv_sub_path (path, "params");

		g_autoptr(GPtrArray) params = NULL;

		if (!lrg_ai_priv_parse_dict (yaml_mapping_get_member (map, "params"), pp, &params,
		                             LRG_BT_ASSET_ERROR, ctx->error))
			return NULL;
		g_ptr_array_unref (node->params);
		node->params = g_steal_pointer (&params);
	}

	/* children: built-ins must use the key their kind takes */
	child = yaml_mapping_get_member (map, "child");
	children = yaml_mapping_get_member (map, "children");
	if (child != NULL && children != NULL)
	{
		pfail (ctx, CODE_INVALID, path, "a node has either 'child' or 'children', not both");
		return NULL;
	}
	if (builtin != NULL && child != NULL && builtin->kind != LRG_BT_NODE_KIND_DECORATOR)
	{
		g_autofree gchar *m = g_strdup_printf ("'%s' takes %s, not 'child'", type,
		                                       builtin->kind == LRG_BT_NODE_KIND_COMPOSITE ? "'children'" : "no children");

		pfail (ctx, CODE_INVALID, path, m);
		return NULL;
	}
	if (builtin != NULL && children != NULL && builtin->kind != LRG_BT_NODE_KIND_COMPOSITE)
	{
		g_autofree gchar *m = g_strdup_printf ("'%s' takes %s, not 'children'", type,
		                                       builtin->kind == LRG_BT_NODE_KIND_DECORATOR ? "one 'child'" : "no children");

		pfail (ctx, CODE_INVALID, path, m);
		return NULL;
	}
	if (child != NULL)
	{
		g_autofree gchar         *cp = lrg_ai_priv_sub_path (path, "child");
		g_autoptr(LrgBtAssetNode) c = parse_node (ctx, child, cp);

		if (c == NULL)
			return NULL;
		lrg_bt_asset_node_insert_child (node, c, -1);
		node->loaded_with = LOADED_CHILD;
	}
	if (children != NULL)
	{
		g_autofree gchar *cp = lrg_ai_priv_sub_path (path, "children");
		YamlSequence     *seq = lrg_ai_priv_as_sequence (children, cp, LRG_BT_ASSET_ERROR, ctx->error);

		if (seq == NULL)
			return NULL;
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			g_autofree gchar         *ep = lrg_ai_priv_index_path (path, "children", i);
			g_autoptr(LrgBtAssetNode) c = parse_node (ctx, yaml_sequence_get_element (seq, i), ep);

			if (c == NULL)
				return NULL;
			lrg_bt_asset_node_insert_child (node, c, -1);
		}
		node->loaded_with = LOADED_CHILDREN;
	}

	/* subtree references are checked once every subtree is declared */
	if (g_strcmp0 (type, "subtree") == 0)
	{
		g_ptr_array_add (ctx->refs, node);
		g_ptr_array_add (ctx->ref_paths, g_strdup (path));
	}

	g_ptr_array_add (known, NULL);
	node->extra = lrg_ai_priv_collect_extra (map, (const gchar * const *) known->pdata);
	return g_steal_pointer (&node);
}

static Subtree *
find_subtree (LrgBtAsset  *self,
              const gchar *name)
{
	guint i;

	for (i = 0; i < self->subtrees->len; i++)
	{
		Subtree *st = g_ptr_array_index (self->subtrees, i);

		if (g_strcmp0 (st->name, name) == 0)
			return st;
	}
	return NULL;
}

/*
 * load_root:
 *
 * Parses a whole document into @tmp (a fresh asset); the caller adopts
 * the contents on success so a failed load leaves the target unchanged.
 */
static gboolean
load_root (LrgBtAsset  *tmp,
           YamlNode    *root,
           GError     **error)
{
	static const gchar * const top_known[] = { "version", "name", "blackboard", "subtrees", "root", NULL };
	static const gchar * const st_known[] = { "path", "root", NULL };
	g_autoptr(GHashTable) ids = g_hash_table_new (g_str_hash, g_str_equal);
	g_autoptr(GPtrArray)  refs = g_ptr_array_new ();
	g_autoptr(GPtrArray)  ref_paths = g_ptr_array_new_with_free_func (g_free);
	Parse                 ctx = { error, ids, refs, ref_paths };
	YamlMapping          *map;
	const gchar          *text;
	guint                 i;

	map = lrg_ai_priv_as_mapping (root, "", LRG_BT_ASSET_ERROR, error);
	if (map == NULL)
	{
		g_clear_error (error);
		g_set_error_literal (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_PARSE, "the document is not a mapping");
		return FALSE;
	}

	/* version: absent means 1 */
	if (!lrg_ai_priv_get_string (map, "", "version", FALSE, &text, LRG_BT_ASSET_ERROR, error))
		return FALSE;
	if (text != NULL)
	{
		gint64 v;

		if (!parse_int_text (text, &v) || v < 1)
			return pfail (&ctx, CODE_INVALID, "version", "expected a positive integer");
		if (v > LRG_BT_ASSET_FORMAT_VERSION)
		{
			g_set_error (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_VERSION,
			             "version: %" G_GINT64_FORMAT " is newer than this build reads (%d)", v,
			             LRG_BT_ASSET_FORMAT_VERSION);
			return FALSE;
		}
	}
	if (!lrg_ai_priv_get_string (map, "", "name", FALSE, &text, LRG_BT_ASSET_ERROR, error))
		return FALSE;
	tmp->name = g_strdup (text);

	if (yaml_mapping_has_member (map, "blackboard") &&
	    !lrg_ai_priv_parse_schema (tmp->schema, yaml_mapping_get_member (map, "blackboard"), "blackboard",
	                               LRG_BT_ASSET_ERROR, error))
		return FALSE;

	/* subtrees: name -> {path: file} or {root: node} */
	if (yaml_mapping_has_member (map, "subtrees"))
	{
		YamlMapping     *sm = lrg_ai_priv_as_mapping (yaml_mapping_get_member (map, "subtrees"), "subtrees",
		                                              LRG_BT_ASSET_ERROR, error);
		g_autoptr(GList) names = NULL;
		GList           *l;

		if (sm == NULL)
			return FALSE;
		names = yaml_mapping_get_members (sm);
		for (l = names; l != NULL; l = l->next)
		{
			g_autofree gchar *sp = lrg_ai_priv_sub_path ("subtrees", l->data);
			YamlMapping      *em = lrg_ai_priv_as_mapping (yaml_mapping_get_member (sm, l->data), sp,
			                                               LRG_BT_ASSET_ERROR, error);
			Subtree          *st;
			const gchar      *path;

			if (em == NULL)
				return FALSE;
			if (!lrg_ai_priv_get_string (em, sp, "path", FALSE, &path, LRG_BT_ASSET_ERROR, error))
				return FALSE;
			if ((path != NULL) == yaml_mapping_has_member (em, "root"))
				return pfail (&ctx, CODE_INVALID, sp, "a subtree has exactly one of 'path' or 'root'");
			st = g_new0 (Subtree, 1);
			st->name = g_strdup (l->data);
			st->path = g_strdup (path);
			g_ptr_array_add (tmp->subtrees, st);
			if (path == NULL)
			{
				g_autofree gchar *rp = lrg_ai_priv_sub_path (sp, "root");

				st->root = parse_node (&ctx, yaml_mapping_get_member (em, "root"), rp);
				if (st->root == NULL)
					return FALSE;
			}
			st->extra = lrg_ai_priv_collect_extra (em, st_known);
		}
	}

	if (yaml_mapping_has_member (map, "root"))
	{
		tmp->root = parse_node (&ctx, yaml_mapping_get_member (map, "root"), "root");
		if (tmp->root == NULL)
			return FALSE;
	}

	/* every subtree node names a declared subtree */
	for (i = 0; i < refs->len; i++)
	{
		LrgBtAssetNode *n = g_ptr_array_index (refs, i);
		GVariant       *ref = lrg_bt_asset_node_get_prop (n, "ref");
		const gchar    *p = g_ptr_array_index (ref_paths, i);

		if (ref == NULL)
			return pfail (&ctx, CODE_INVALID, p, "a subtree node needs 'ref'");
		if (find_subtree (tmp, g_variant_get_string (ref, NULL)) == NULL)
		{
			g_autofree gchar *rp = lrg_ai_priv_sub_path (p, "ref");
			g_autofree gchar *m = g_strdup_printf ("unknown subtree '%s' (declare it under subtrees:)",
			                                       g_variant_get_string (ref, NULL));

			return pfail (&ctx, CODE_REFERENCE, rp, m);
		}
	}

	tmp->extra = lrg_ai_priv_collect_extra (map, top_known);
	return TRUE;
}

/* moves @tmp's contents into @self (the base dir and options stay) */
static void
adopt_contents (LrgBtAsset *self,
                LrgBtAsset *tmp)
{
	g_free (self->name);
	self->name = g_steal_pointer (&tmp->name);
	g_clear_object (&self->schema);
	self->schema = g_steal_pointer (&tmp->schema);
	tmp->schema = lrg_blackboard_schema_new ();
	g_clear_object (&self->root);
	self->root = g_steal_pointer (&tmp->root);
	g_ptr_array_unref (self->subtrees);
	self->subtrees = g_steal_pointer (&tmp->subtrees);
	tmp->subtrees = g_ptr_array_new_with_free_func (subtree_free);
	g_clear_pointer (&self->extra, yaml_mapping_unref);
	self->extra = g_steal_pointer (&tmp->extra);
	g_hash_table_remove_all (self->loaded);
}

gboolean
lrg_bt_asset_load_from_data (LrgBtAsset   *self,
                             const gchar  *data,
                             gssize        length,
                             GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError)     local = NULL;
	g_autoptr(LrgBtAsset) tmp = NULL;
	YamlNode             *root;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), FALSE);
	g_return_val_if_fail (data != NULL, FALSE);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_PARSE, "%s", local->message);
		return FALSE;
	}
	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error_literal (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_PARSE, "the document is empty");
		return FALSE;
	}
	tmp = lrg_bt_asset_new ();
	if (!load_root (tmp, root, error))
		return FALSE;
	adopt_contents (self, tmp);
	return TRUE;
}

gboolean
lrg_bt_asset_load_from_file (LrgBtAsset   *self,
                             const gchar  *path,
                             GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *dir = NULL;
	g_autoptr(GError) local = NULL;
	gsize             len = 0;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	if (!lrg_bt_asset_load_from_data (self, text, (gssize) len, error))
	{
		g_prefix_error (error, "%s: ", path);
		return FALSE;
	}
	dir = g_path_get_dirname (path);
	lrg_bt_asset_set_base_dir (self, dir);
	return TRUE;
}

LrgBtAsset *
lrg_bt_asset_new_from_file (const gchar  *path,
                            GError      **error)
{
	g_autoptr(LrgBtAsset) self = lrg_bt_asset_new ();

	if (!lrg_bt_asset_load_from_file (self, path, error))
		return NULL;
	return g_steal_pointer (&self);
}

LrgBtAsset *
lrg_bt_asset_new_from_data (const gchar  *data,
                            gssize        length,
                            GError      **error)
{
	g_autoptr(LrgBtAsset) self = lrg_bt_asset_new ();

	if (!lrg_bt_asset_load_from_data (self, data, length, error))
		return NULL;
	return g_steal_pointer (&self);
}

/* --- writing ----------------------------------------------------------- */

static YamlNode *
write_node (LrgBtAssetNode *node)
{
	YamlNode     *ynode = yaml_node_new_mapping (NULL);
	YamlMapping  *map = yaml_node_get_mapping (ynode);
	LrgBtNodeKind kind = lrg_bt_asset_node_get_kind (node);
	guint         i;

	yaml_mapping_set_string_member (map, "type", node->type_name);
	if (node->id_explicit && node->id != NULL)
		lrg_ai_priv_put (map, "id", lrg_ai_priv_variant_to_node (g_variant_new_string (node->id)));
	if (node->name != NULL)
		lrg_ai_priv_put (map, "name", lrg_ai_priv_variant_to_node (g_variant_new_string (node->name)));
	{
		g_auto(GStrv) keys = lrg_ai_priv_dict_keys (node->props);

		for (i = 0; keys[i] != NULL; i++)
			lrg_ai_priv_put (map, keys[i], lrg_ai_priv_variant_to_node (lrg_ai_priv_dict_lookup (node->props, keys[i])));
	}
	if (node->params->len > 0)
		lrg_ai_priv_put (map, "params", lrg_ai_priv_dict_to_node (node->params));

	/* one child of a decorator goes under child:, everything else under
	 * children: */
	if (node->children->len == 1 && kind == LRG_BT_NODE_KIND_DECORATOR)
		lrg_ai_priv_put (map, "child", write_node (g_ptr_array_index (node->children, 0)));
	else if (node->children->len > 0)
	{
		YamlNode     *cnode = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (cnode);

		for (i = 0; i < node->children->len; i++)
		{
			YamlNode *e = write_node (g_ptr_array_index (node->children, i));

			yaml_sequence_add_element (seq, e);
			yaml_node_unref (e);
		}
		lrg_ai_priv_put (map, "children", cnode);
	}
	lrg_ai_priv_copy_extra (map, node->extra);
	return ynode;
}

gchar *
lrg_bt_asset_to_data (LrgBtAsset  *self,
                      GError     **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	YamlMapping             *map;
	YamlNode                *schema;
	guint                    i;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);

	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_int_member (map, "version", LRG_BT_ASSET_FORMAT_VERSION);
	if (self->name != NULL)
		lrg_ai_priv_put (map, "name", lrg_ai_priv_variant_to_node (g_variant_new_string (self->name)));
	schema = lrg_ai_priv_write_schema (self->schema);
	if (schema != NULL)
		lrg_ai_priv_put (map, "blackboard", schema);
	if (self->subtrees->len > 0)
	{
		YamlNode    *snode = yaml_node_new_mapping (NULL);
		YamlMapping *smap = yaml_node_get_mapping (snode);

		for (i = 0; i < self->subtrees->len; i++)
		{
			Subtree     *st = g_ptr_array_index (self->subtrees, i);
			YamlNode    *enode = yaml_node_new_mapping (NULL);
			YamlMapping *emap = yaml_node_get_mapping (enode);

			if (st->path != NULL)
				yaml_mapping_set_string_member (emap, "path", st->path);
			else if (st->root != NULL)
				lrg_ai_priv_put (emap, "root", write_node (st->root));
			lrg_ai_priv_copy_extra (emap, st->extra);
			lrg_ai_priv_put (smap, st->name, enode);
		}
		lrg_ai_priv_put (map, "subtrees", snode);
	}
	if (self->root != NULL)
		lrg_ai_priv_put (map, "root", write_node (self->root));
	lrg_ai_priv_copy_extra (map, self->extra);

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, NULL, error);
}

gboolean
lrg_bt_asset_save_to_file (LrgBtAsset   *self,
                           const gchar  *path,
                           GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_bt_asset_to_data (self, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, -1, &local))
	{
		g_set_error (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return TRUE;
}

/* --- accessors --------------------------------------------------------- */

const gchar *
lrg_bt_asset_get_name (LrgBtAsset *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);
	return self->name;
}

void
lrg_bt_asset_set_name (LrgBtAsset  *self,
                       const gchar *name)
{
	g_return_if_fail (LRG_IS_BT_ASSET (self));

	g_free (self->name);
	self->name = g_strdup (name);
}

const gchar *
lrg_bt_asset_get_base_dir (LrgBtAsset *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);
	return self->base_dir;
}

void
lrg_bt_asset_set_base_dir (LrgBtAsset  *self,
                           const gchar *dir)
{
	g_return_if_fail (LRG_IS_BT_ASSET (self));

	g_free (self->base_dir);
	self->base_dir = g_strdup (dir);
	g_hash_table_remove_all (self->loaded);
}

LrgBlackboardSchema *
lrg_bt_asset_get_schema (LrgBtAsset *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);
	return self->schema;
}

LrgBtAssetNode *
lrg_bt_asset_get_root (LrgBtAsset *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);
	return self->root;
}

void
lrg_bt_asset_set_root (LrgBtAsset     *self,
                       LrgBtAssetNode *root)
{
	g_return_if_fail (LRG_IS_BT_ASSET (self));
	g_return_if_fail (root == NULL || LRG_IS_BT_ASSET_NODE (root));
	g_return_if_fail (root == NULL || root->parent == NULL);

	g_set_object (&self->root, root);
}

/* collects the ids in use (explicit and generated) */
static void
collect_ids (LrgBtAssetNode *node,
             GHashTable     *ids)
{
	guint i;

	if (node == NULL)
		return;
	if (node->id != NULL)
		g_hash_table_add (ids, node->id);
	for (i = 0; i < node->children->len; i++)
		collect_ids (g_ptr_array_index (node->children, i), ids);
}

/* gives every node without an id the next free n<k> */
static void
assign_ids (LrgBtAssetNode *node,
            GHashTable     *ids,
            guint          *counter)
{
	guint i;

	if (node == NULL)
		return;
	if (node->id == NULL)
	{
		gchar *id = NULL;

		do
		{
			g_free (id);
			id = g_strdup_printf ("n%u", (*counter)++);
		}
		while (g_hash_table_contains (ids, id));
		node->id = id;
		node->id_explicit = FALSE;
		g_hash_table_add (ids, node->id);
	}
	for (i = 0; i < node->children->len; i++)
		assign_ids (g_ptr_array_index (node->children, i), ids, counter);
}

static void
ensure_ids (LrgBtAsset *self)
{
	g_autoptr(GHashTable) ids = g_hash_table_new (g_str_hash, g_str_equal);
	guint                 counter = 0;
	guint                 i;

	collect_ids (self->root, ids);
	for (i = 0; i < self->subtrees->len; i++)
		collect_ids (((Subtree *) g_ptr_array_index (self->subtrees, i))->root, ids);
	assign_ids (self->root, ids, &counter);
	for (i = 0; i < self->subtrees->len; i++)
		assign_ids (((Subtree *) g_ptr_array_index (self->subtrees, i))->root, ids, &counter);
}

static LrgBtAssetNode *
find_in (LrgBtAssetNode *node,
         const gchar    *id)
{
	guint i;

	if (node == NULL)
		return NULL;
	if (g_strcmp0 (node->id, id) == 0)
		return node;
	for (i = 0; i < node->children->len; i++)
	{
		LrgBtAssetNode *found = find_in (g_ptr_array_index (node->children, i), id);

		if (found != NULL)
			return found;
	}
	return NULL;
}

LrgBtAssetNode *
lrg_bt_asset_find_node (LrgBtAsset  *self,
                        const gchar *id)
{
	LrgBtAssetNode *found;
	guint           i;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);

	ensure_ids (self);
	found = find_in (self->root, id);
	for (i = 0; found == NULL && i < self->subtrees->len; i++)
		found = find_in (((Subtree *) g_ptr_array_index (self->subtrees, i))->root, id);
	return found;
}

/* the named subtree, created (empty) at the end when new */
static Subtree *
subtree_slot (LrgBtAsset  *self,
              const gchar *name)
{
	Subtree *st = find_subtree (self, name);

	if (st == NULL)
	{
		st = g_new0 (Subtree, 1);
		st->name = g_strdup (name);
		g_ptr_array_add (self->subtrees, st);
	}
	return st;
}

void
lrg_bt_asset_set_subtree_path (LrgBtAsset  *self,
                               const gchar *name,
                               const gchar *path)
{
	Subtree *st;

	g_return_if_fail (LRG_IS_BT_ASSET (self));
	g_return_if_fail (name != NULL && *name != '\0');
	g_return_if_fail (path != NULL);

	st = subtree_slot (self, name);
	g_free (st->path);
	st->path = g_strdup (path);
	g_clear_object (&st->root);
	g_hash_table_remove_all (self->loaded);
}

void
lrg_bt_asset_set_subtree_root (LrgBtAsset     *self,
                               const gchar    *name,
                               LrgBtAssetNode *root)
{
	Subtree *st;

	g_return_if_fail (LRG_IS_BT_ASSET (self));
	g_return_if_fail (name != NULL && *name != '\0');
	g_return_if_fail (LRG_IS_BT_ASSET_NODE (root));
	g_return_if_fail (root->parent == NULL);

	st = subtree_slot (self, name);
	g_clear_pointer (&st->path, g_free);
	g_set_object (&st->root, root);
}

gboolean
lrg_bt_asset_remove_subtree (LrgBtAsset  *self,
                             const gchar *name)
{
	Subtree *st;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), FALSE);

	st = name != NULL ? find_subtree (self, name) : NULL;
	if (st == NULL)
		return FALSE;
	g_ptr_array_remove (self->subtrees, st);
	return TRUE;
}

gchar **
lrg_bt_asset_dup_subtree_names (LrgBtAsset *self)
{
	GPtrArray *out;
	guint      i;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);

	out = g_ptr_array_new ();
	for (i = 0; i < self->subtrees->len; i++)
		g_ptr_array_add (out, g_strdup (((Subtree *) g_ptr_array_index (self->subtrees, i))->name));
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

const gchar *
lrg_bt_asset_get_subtree_path (LrgBtAsset  *self,
                               const gchar *name)
{
	Subtree *st;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);

	st = name != NULL ? find_subtree (self, name) : NULL;
	return st != NULL ? st->path : NULL;
}

LrgBtAssetNode *
lrg_bt_asset_get_subtree_root (LrgBtAsset  *self,
                               const gchar *name)
{
	Subtree *st;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);

	st = name != NULL ? find_subtree (self, name) : NULL;
	return st != NULL ? st->root : NULL;
}

gboolean
lrg_bt_asset_get_allow_unbound (LrgBtAsset *self)
{
	g_return_val_if_fail (LRG_IS_BT_ASSET (self), FALSE);
	return self->allow_unbound;
}

void
lrg_bt_asset_set_allow_unbound (LrgBtAsset *self,
                                gboolean    allow)
{
	g_return_if_fail (LRG_IS_BT_ASSET (self));
	self->allow_unbound = allow;
}

/* ==========================================================================
 * 6. Building
 * ========================================================================== */

/* a built node and the asset node id it came from */
typedef struct
{
	gchar     *id;
	LrgBTNode *node;
	gboolean   alias;     /* a subtree node's id naming its expansion */
	guint64    ticks;     /* tick count before the last tick */
	gboolean   visited;
} Record;

static void
record_free (gpointer data)
{
	Record *r = data;

	g_free (r->id);
	g_clear_object (&r->node);
	g_free (r);
}

typedef struct
{
	LrgBtAsset     *top;
	LrgScripting   *scripting;
	LrgBlackboard  *blackboard;
	GPtrArray      *records;    /* Record, or NULL */
	GPtrArray      *missing;    /* unbound names, or NULL (then strict) */
	GPtrArray      *stack;      /* subtree keys being expanded */
	gboolean        lenient;    /* missing bindings build as failing leaves */
	GError        **error;
} Build;

/* what a bound leaf does */
typedef enum
{
	LEAF_ACTION,
	LEAF_CONDITION,
	LEAF_SCRIPT_ACTION,
	LEAF_SCRIPT_CONDITION,
	LEAF_CHECK,
	LEAF_SET,
	LEAF_UNBOUND
} LeafKind;

typedef struct
{
	LeafKind         kind;
	LrgAiBound      *bound;
	LrgScripting    *scripting;
	gchar           *func;
	GVariant        *params;    /* a{sv}, never NULL */
	gchar           *key;
	LrgBlackboardOp  op;
	GVariant        *value;
	gchar           *label;     /* for messages */
	gboolean         warned;
} Leaf;

static void
leaf_free (gpointer data)
{
	Leaf *leaf = data;

	lrg_ai_priv_bound_unref (leaf->bound);
	g_clear_object (&leaf->scripting);
	g_free (leaf->func);
	g_clear_pointer (&leaf->params, g_variant_unref);
	g_free (leaf->key);
	g_clear_pointer (&leaf->value, g_variant_unref);
	g_free (leaf->label);
	g_free (leaf);
}

/* an action leaf's tick */
static LrgBTStatus
leaf_act (LrgBlackboard *blackboard,
          gfloat         delta_time,
          gpointer       user_data)
{
	Leaf *leaf = user_data;

	switch (leaf->kind)
	{
	case LEAF_ACTION:
		return lrg_ai_priv_bound_act (leaf->bound, blackboard, delta_time, leaf->params);

	case LEAF_SCRIPT_ACTION:
	{
		GValue            ret = G_VALUE_INIT;
		g_autoptr(GError) error = NULL;
		LrgBTStatus       status;

		if (!lrg_ai_priv_call_script (leaf->scripting, leaf->func, blackboard, delta_time, leaf->params, &ret,
		                              &error))
		{
			if (!leaf->warned)
				lrg_warning (LRG_LOG_DOMAIN, "behaviour tree %s: script '%s' failed: %s", leaf->label, leaf->func,
				             error != NULL ? error->message : "unknown error");
			leaf->warned = TRUE;
			if (G_IS_VALUE (&ret))
				g_value_unset (&ret);
			return LRG_BT_STATUS_FAILURE;
		}
		status = lrg_ai_priv_status_of_value (&ret);
		if (G_IS_VALUE (&ret))
			g_value_unset (&ret);
		return status;
	}

	case LEAF_SET:
		lrg_blackboard_set_variant (blackboard, leaf->key, leaf->value);
		return LRG_BT_STATUS_SUCCESS;

	case LEAF_UNBOUND:
	default:
		if (!leaf->warned)
			lrg_warning (LRG_LOG_DOMAIN, "behaviour tree %s is not bound; it fails", leaf->label);
		leaf->warned = TRUE;
		return LRG_BT_STATUS_FAILURE;
	}
}

/* a condition leaf's test */
static gboolean
leaf_test (LrgBlackboard *blackboard,
           gpointer       user_data)
{
	Leaf *leaf = user_data;

	switch (leaf->kind)
	{
	case LEAF_CONDITION:
		return lrg_ai_priv_bound_test (leaf->bound, blackboard, leaf->params);

	case LEAF_SCRIPT_CONDITION:
	{
		GValue            ret = G_VALUE_INIT;
		g_autoptr(GError) error = NULL;
		gboolean          truth;

		if (!lrg_ai_priv_call_script (leaf->scripting, leaf->func, blackboard, 0.0f, leaf->params, &ret, &error))
		{
			if (!leaf->warned)
				lrg_warning (LRG_LOG_DOMAIN, "behaviour tree %s: script '%s' failed: %s", leaf->label, leaf->func,
				             error != NULL ? error->message : "unknown error");
			leaf->warned = TRUE;
			if (G_IS_VALUE (&ret))
				g_value_unset (&ret);
			return FALSE;
		}
		truth = lrg_ai_priv_truth_of_value (&ret);
		if (G_IS_VALUE (&ret))
			g_value_unset (&ret);
		return truth;
	}

	case LEAF_CHECK:
		return lrg_blackboard_test (blackboard, leaf->key, leaf->op, leaf->value);

	case LEAF_UNBOUND:
	default:
		if (!leaf->warned)
			lrg_warning (LRG_LOG_DOMAIN, "behaviour tree %s is not bound; it fails", leaf->label);
		leaf->warned = TRUE;
		return FALSE;
	}
}

static gboolean
bfail (Build       *b,
       gint         code,
       const gchar *path,
       const gchar *msg)
{
	return lrg_ai_priv_fail (b->error, LRG_BT_ASSET_ERROR, code, path, "%s", msg);
}

/* records a missing binding (once); in strict mode that is an error */
static gboolean
note_missing (Build       *b,
              const gchar *what,
              const gchar *name,
              const gchar *path)
{
	g_autofree gchar *entry = g_strdup_printf ("%s:%s", what, name);
	guint             i;

	if (b->missing != NULL)
	{
		for (i = 0; i < b->missing->len; i++)
			if (g_strcmp0 (g_ptr_array_index (b->missing, i), entry) == 0)
				return TRUE;
		g_ptr_array_add (b->missing, g_steal_pointer (&entry));
		return TRUE;
	}
	if (b->lenient)
		return TRUE;
	{
		g_autofree gchar *m = g_strdup_printf ("%s '%s' is not %s", what, name,
		                                       g_strcmp0 (what, "type") == 0 ? "a known node type" : "registered");

		return bfail (b, LRG_BT_ASSET_ERROR_BINDING, path, m);
	}
}

static const gchar *
prop_string (LrgBtAssetNode *node,
             const gchar    *key)
{
	GVariant *v = lrg_bt_asset_node_get_prop (node, key);

	return v != NULL && g_variant_is_of_type (v, G_VARIANT_TYPE_STRING) ? g_variant_get_string (v, NULL) : NULL;
}

/* a numeric property (ints and doubles), or @def */
static gdouble
prop_number (LrgBtAssetNode *node,
             const gchar    *key,
             gdouble         def,
             gboolean       *present)
{
	GVariant *v = lrg_bt_asset_node_get_prop (node, key);

	if (present != NULL)
		*present = FALSE;
	if (v == NULL)
		return def;
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
	{
		if (present != NULL)
			*present = TRUE;
		return g_variant_get_double (v);
	}
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT64))
	{
		if (present != NULL)
			*present = TRUE;
		return (gdouble) g_variant_get_int64 (v);
	}
	if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT32))
	{
		if (present != NULL)
			*present = TRUE;
		return (gdouble) g_variant_get_int32 (v);
	}
	return def;
}

/* the node's path for messages: its id, or its type */
static gchar *
node_label (LrgBtAssetNode *node,
            const gchar    *prefix)
{
	return g_strdup_printf ("node '%s%s' (%s)", prefix, node->id != NULL ? node->id : "?", node->type_name);
}

/* a check/guard/set key: required, declared when the schema is not empty */
static gboolean
check_key (Build          *b,
           LrgBtAsset     *asset,
           LrgBtAssetNode *node,
           const gchar    *label,
           const gchar   **out_key)
{
	const gchar *key = prop_string (node, "key");

	*out_key = key;
	if (key == NULL || *key == '\0')
		return bfail (b, CODE_INVALID, label, "missing 'key'");
	if (lrg_blackboard_schema_get_key_count (asset->schema) > 0 &&
	    !lrg_blackboard_schema_has_key (asset->schema, key))
	{
		g_autofree gchar *m = g_strdup_printf ("blackboard key '%s' is not declared under blackboard:", key);

		return bfail (b, CODE_REFERENCE, label, m);
	}
	return TRUE;
}

/* the test of a check/guard node */
static gboolean
node_test (Build           *b,
           LrgBtAssetNode  *node,
           const gchar     *label,
           LrgBlackboardOp *out_op,
           GVariant       **out_value)
{
	const gchar *op_text = prop_string (node, "op");

	*out_value = lrg_bt_asset_node_get_prop (node, "value");
	if (op_text == NULL)
		*out_op = *out_value != NULL ? LRG_BLACKBOARD_OP_EQ : LRG_BLACKBOARD_OP_SET;
	else if (!lrg_blackboard_op_from_string (op_text, out_op))
	{
		g_autofree gchar *m = g_strdup_printf ("unknown operator '%s'", op_text);

		return bfail (b, CODE_INVALID, label, m);
	}
	if (*out_op != LRG_BLACKBOARD_OP_SET && *out_op != LRG_BLACKBOARD_OP_UNSET && *out_value == NULL)
		return bfail (b, CODE_INVALID, label, "the operator needs a 'value'");
	return TRUE;
}

/* sets a custom node's properties from the asset node's (by name) */
static gboolean
apply_custom_props (Build          *b,
                    GObject        *object,
                    LrgBtAssetNode *node,
                    const gchar    *label)
{
	g_auto(GStrv) keys = lrg_ai_priv_dict_keys (node->props);
	guint         i;

	for (i = 0; keys[i] != NULL; i++)
	{
		const gchar      *k = keys[i];
		GVariant         *v = lrg_ai_priv_dict_lookup (node->props, k);
		g_autofree gchar *pname = g_strdelimit (g_strdup (k), "_", '-');
		GParamSpec       *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object), pname);
		GValue            gv = G_VALUE_INIT;
		GValue            src = G_VALUE_INIT;
		gboolean          ok = FALSE;

		if (pspec == NULL || !(pspec->flags & G_PARAM_WRITABLE))
		{
			g_autofree gchar *m = g_strdup_printf ("'%s' has no writable property '%s'", G_OBJECT_TYPE_NAME (object),
			                                       pname);

			return bfail (b, CODE_INVALID, label, m);
		}
		g_value_init (&gv, pspec->value_type);
		if (G_IS_PARAM_SPEC_ENUM (pspec) && g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
		{
			GEnumClass *ec = g_type_class_ref (pspec->value_type);
			GEnumValue *ev = g_enum_get_value_by_nick (ec, g_variant_get_string (v, NULL));

			if (ev != NULL)
			{
				g_value_set_enum (&gv, ev->value);
				ok = TRUE;
			}
			g_type_class_unref (ec);
		}
		else
		{
			/* the variant as a GValue, then a GLib transform */
			if (g_variant_is_of_type (v, G_VARIANT_TYPE_BOOLEAN))
			{
				g_value_init (&src, G_TYPE_BOOLEAN);
				g_value_set_boolean (&src, g_variant_get_boolean (v));
			}
			else if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT64))
			{
				g_value_init (&src, G_TYPE_INT64);
				g_value_set_int64 (&src, g_variant_get_int64 (v));
			}
			else if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
			{
				g_value_init (&src, G_TYPE_DOUBLE);
				g_value_set_double (&src, g_variant_get_double (v));
			}
			else if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
			{
				g_value_init (&src, G_TYPE_STRING);
				g_value_set_string (&src, g_variant_get_string (v, NULL));
			}
			if (G_IS_VALUE (&src) && g_value_type_transformable (G_VALUE_TYPE (&src), pspec->value_type))
				ok = g_value_transform (&src, &gv);
			if (G_IS_VALUE (&src))
				g_value_unset (&src);
		}
		if (ok)
			g_object_set_property (object, pspec->name, &gv);
		g_value_unset (&gv);
		if (!ok)
		{
			g_autofree gchar *m = g_strdup_printf ("property '%s' cannot take that value", pname);

			return bfail (b, CODE_INVALID, label, m);
		}
	}
	return TRUE;
}

static LrgBTNode *build_node (Build *b, LrgBtAsset *asset, LrgBtAssetNode *node, const gchar *prefix);

/* resolves a file subtree to its (cached) asset */
static LrgBtAsset *
subtree_asset (Build       *b,
               LrgBtAsset  *asset,
               Subtree     *st,
               const gchar *label)
{
	g_autofree gchar *full = NULL;
	LrgBtAsset       *sub;
	GError           *local = NULL;

	if (g_path_is_absolute (st->path))
		full = g_canonicalize_filename (st->path, NULL);
	else
		full = g_canonicalize_filename (st->path, asset->base_dir);
	sub = g_hash_table_lookup (b->top->loaded, full);
	if (sub != NULL)
		return sub;
	sub = lrg_bt_asset_new_from_file (full, &local);
	if (sub == NULL)
	{
		g_autofree gchar *m = g_strdup_printf ("subtree '%s' (%s): %s", st->name, st->path, local->message);

		g_error_free (local);
		bfail (b, CODE_REFERENCE, label, m);
		return NULL;
	}
	g_hash_table_insert (b->top->loaded, g_strdup (full), sub);
	return sub;
}

/* expands a subtree node into its subtree's built root */
static LrgBTNode *
build_subtree (Build          *b,
               LrgBtAsset     *asset,
               LrgBtAssetNode *node,
               const gchar    *prefix,
               const gchar    *label)
{
	const gchar      *ref = prop_string (node, "ref");
	Subtree          *st;
	LrgBtAsset       *owner = asset;
	LrgBtAssetNode   *sroot;
	g_autofree gchar *key = NULL;
	g_autofree gchar *inner = NULL;
	LrgBTNode        *built;
	guint             i;

	if (ref == NULL)
	{
		bfail (b, CODE_INVALID, label, "a subtree node needs 'ref'");
		return NULL;
	}
	st = find_subtree (asset, ref);
	if (st == NULL)
	{
		g_autofree gchar *m = g_strdup_printf ("unknown subtree '%s'", ref);

		bfail (b, CODE_REFERENCE, label, m);
		return NULL;
	}
	if (node->children->len > 0)
	{
		bfail (b, CODE_INVALID, label, "a subtree node has no children");
		return NULL;
	}

	/* the subtree's root and the asset its names resolve in */
	if (st->path != NULL)
	{
		owner = subtree_asset (b, asset, st, label);
		if (owner == NULL)
			return NULL;
		ensure_ids (owner);
		sroot = owner->root;
		key = g_strdup_printf ("file:%p", (gpointer) owner);
		if (b->blackboard != NULL)
			lrg_blackboard_schema_apply_defaults (owner->schema, b->blackboard, FALSE);
	}
	else
	{
		sroot = st->root;
		key = g_strdup_printf ("inline:%p:%s", (gpointer) asset, st->name);
	}
	if (sroot == NULL)
	{
		g_autofree gchar *m = g_strdup_printf ("subtree '%s' has no root", ref);

		bfail (b, CODE_INVALID, label, m);
		return NULL;
	}

	/* a subtree that (indirectly) contains itself never ends */
	for (i = 0; i < b->stack->len; i++)
	{
		if (g_strcmp0 (g_ptr_array_index (b->stack, i), key) == 0)
		{
			g_autofree gchar *m = g_strdup_printf ("subtree '%s' contains itself", ref);

			bfail (b, CODE_REFERENCE, label, m);
			return NULL;
		}
	}
	g_ptr_array_add (b->stack, g_strdup (key));
	inner = g_strdup_printf ("%s%s/", prefix, node->id);
	if (b->records != NULL)
	{
		/* the subtree node's own id names the expansion (an alias); the
		 * real record follows with the inner ids */
		Record *r = g_new0 (Record, 1);

		r->id = g_strdup_printf ("%s%s", prefix, node->id);
		r->alias = TRUE;
		g_ptr_array_add (b->records, r);
		built = build_node (b, owner, sroot, inner);
		if (built != NULL)
			r->node = g_object_ref (built);
	}
	else
		built = build_node (b, owner, sroot, inner);
	g_free (g_ptr_array_steal_index (b->stack, b->stack->len - 1));
	return built;
}

/* builds one leaf node */
static LrgBTNode *
build_leaf (Build          *b,
            LrgBtAsset     *asset,
            LrgBtAssetNode *node,
            const gchar    *label)
{
	const gchar *t = node->type_name;
	Leaf        *leaf;
	gboolean     is_action = g_strcmp0 (t, "action") == 0;
	gboolean     is_condition = g_strcmp0 (t, "condition") == 0;

	if (g_strcmp0 (t, "wait") == 0)
	{
		gboolean present;
		gdouble  d = prop_number (node, "duration", 0.0, &present);

		if (!present)
		{
			bfail (b, CODE_INVALID, label, "missing 'duration'");
			return NULL;
		}
		return LRG_BT_NODE (lrg_bt_wait_new ((gfloat) d));
	}

	leaf = g_new0 (Leaf, 1);
	leaf->label = g_strdup (label);
	leaf->params = lrg_ai_priv_dict_to_variant (node->params);

	if (g_strcmp0 (t, "check") == 0)
	{
		const gchar *key;
		GVariant    *value;

		if (!check_key (b, asset, node, label, &key) || !node_test (b, node, label, &leaf->op, &value))
		{
			leaf_free (leaf);
			return NULL;
		}
		leaf->kind = LEAF_CHECK;
		leaf->key = g_strdup (key);
		leaf->value = value != NULL ? g_variant_ref (value) : NULL;
		return LRG_BT_NODE (lrg_bt_condition_new (leaf_test, leaf, leaf_free));
	}
	if (g_strcmp0 (t, "set") == 0)
	{
		const gchar *key;
		GVariant    *value = lrg_bt_asset_node_get_prop (node, "value");

		if (!check_key (b, asset, node, label, &key))
		{
			leaf_free (leaf);
			return NULL;
		}
		if (value == NULL)
		{
			leaf_free (leaf);
			bfail (b, CODE_INVALID, label, "missing 'value'");
			return NULL;
		}
		leaf->kind = LEAF_SET;
		leaf->key = g_strdup (key);
		leaf->value = g_variant_ref (value);
		return LRG_BT_NODE (lrg_bt_action_new (leaf_act, leaf, leaf_free));
	}

	if (is_action || is_condition)
	{
		const gchar *name = prop_string (node, is_action ? "action" : "condition");
		const gchar *script = prop_string (node, "script");

		if ((name != NULL) == (script != NULL))
		{
			g_autofree gchar *m = g_strdup_printf ("needs exactly one of '%s' or 'script'",
			                                       is_action ? "action" : "condition");

			leaf_free (leaf);
			bfail (b, CODE_INVALID, label, m);
			return NULL;
		}
		if (name != NULL)
		{
			leaf->bound = is_action ? lrg_ai_priv_lookup_action (name) : lrg_ai_priv_lookup_condition (name);
			leaf->kind = is_action ? LEAF_ACTION : LEAF_CONDITION;
			if (leaf->bound == NULL)
			{
				leaf->kind = LEAF_UNBOUND;
				if (!note_missing (b, is_action ? "action" : "condition", name, label))
				{
					leaf_free (leaf);
					return NULL;
				}
			}
		}
		else
		{
			leaf->kind = is_action ? LEAF_SCRIPT_ACTION : LEAF_SCRIPT_CONDITION;
			leaf->func = g_strdup (script);
			if (b->scripting != NULL && lrg_scripting_has_function (b->scripting, script))
				leaf->scripting = g_object_ref (b->scripting);
			else
			{
				leaf->kind = LEAF_UNBOUND;
				if (!note_missing (b, "script", script, label))
				{
					leaf_free (leaf);
					return NULL;
				}
			}
		}
		if (is_action)
			return LRG_BT_NODE (lrg_bt_action_new (leaf_act, leaf, leaf_free));
		return LRG_BT_NODE (lrg_bt_condition_new (leaf_test, leaf, leaf_free));
	}

	leaf_free (leaf);
	{
		g_autofree gchar *m = g_strdup_printf ("'%s' is not a leaf type", t);

		bfail (b, CODE_INVALID, label, m);
	}
	return NULL;
}

/* builds a built-in composite or decorator around its built children */
static LrgBTNode *
build_inner (Build          *b,
             LrgBtAssetNode *node,
             const gchar    *label,
             GPtrArray      *kids)
{
	const gchar *t = node->type_name;
	LrgBTNode   *child = kids->len > 0 ? g_ptr_array_index (kids, 0) : NULL;
	gboolean     present;
	gdouble      d;

	if (g_strcmp0 (t, "sequence") == 0 || g_strcmp0 (t, "selector") == 0 || g_strcmp0 (t, "parallel") == 0)
	{
		LrgBTComposite *c;
		guint           i;

		if (g_strcmp0 (t, "sequence") == 0)
			c = LRG_BT_COMPOSITE (lrg_bt_sequence_new ());
		else if (g_strcmp0 (t, "selector") == 0)
			c = LRG_BT_COMPOSITE (lrg_bt_selector_new ());
		else
		{
			const gchar *policy = prop_string (node, "policy");

			if (policy != NULL && g_strcmp0 (policy, "one") != 0 && g_strcmp0 (policy, "all") != 0)
			{
				bfail (b, CODE_INVALID, label, "policy is 'one' or 'all'");
				return NULL;
			}
			c = LRG_BT_COMPOSITE (lrg_bt_parallel_new (g_strcmp0 (policy, "one") == 0 ?
			                                           LRG_BT_PARALLEL_REQUIRE_ONE : LRG_BT_PARALLEL_REQUIRE_ALL));
		}
		for (i = 0; i < kids->len; i++)
			lrg_bt_composite_add_child (c, g_ptr_array_index (kids, i));
		return LRG_BT_NODE (c);
	}

	/* decorators */
	if (g_strcmp0 (t, "inverter") == 0)
		return LRG_BT_NODE (lrg_bt_inverter_new (child));
	if (g_strcmp0 (t, "succeeder") == 0)
		return LRG_BT_NODE (lrg_bt_succeeder_new (child));
	if (g_strcmp0 (t, "failer") == 0)
		return LRG_BT_NODE (lrg_bt_failer_new (child));
	if (g_strcmp0 (t, "repeater") == 0)
		return LRG_BT_NODE (lrg_bt_repeater_new (child, (guint) prop_number (node, "count", 1.0, NULL)));
	if (g_strcmp0 (t, "cooldown") == 0 || g_strcmp0 (t, "time_limit") == 0)
	{
		d = prop_number (node, "seconds", 0.0, &present);
		if (!present)
		{
			bfail (b, CODE_INVALID, label, "missing 'seconds'");
			return NULL;
		}
		if (g_strcmp0 (t, "cooldown") == 0)
			return LRG_BT_NODE (lrg_bt_cooldown_new (child, (gfloat) d));
		return LRG_BT_NODE (lrg_bt_time_limit_new (child, (gfloat) d));
	}
	if (g_strcmp0 (t, "guard") == 0)
	{
		LrgBlackboardOp op;
		GVariant       *value;
		const gchar    *key = prop_string (node, "key");

		if (key == NULL)
		{
			bfail (b, CODE_INVALID, label, "missing 'key'");
			return NULL;
		}
		if (!node_test (b, node, label, &op, &value))
			return NULL;
		return LRG_BT_NODE (lrg_bt_guard_new (child, key, op, value));
	}
	return NULL;
}

/*
 * build_node:
 *
 * Builds one asset node (and its subtree) into a runtime node. @prefix is
 * the id prefix of the subtree expansion being built ("" at the top).
 * Records the node (by its full id) before its children so the record
 * array is in depth-first, root-first order.
 */
static LrgBTNode *
build_node (Build          *b,
            LrgBtAsset     *asset,
            LrgBtAssetNode *node,
            const gchar    *prefix)
{
	g_autofree gchar     *label = node_label (node, prefix);
	g_autoptr(GPtrArray)  kids = NULL;
	g_autoptr(LrgBTNode)  built = NULL;
	const Builtin        *builtin = find_builtin (node->type_name);
	GType                 custom = G_TYPE_INVALID;
	LrgBtNodeKind         kind;
	Record               *record = NULL;
	guint                 i;

	/* a subtree is replaced by its expansion */
	if (builtin != NULL && builtin->kind == LRG_BT_NODE_KIND_SUBTREE)
		return build_subtree (b, asset, node, prefix, label);

	if (builtin == NULL)
		custom = lookup_custom_type (node->type_name);
	kind = builtin != NULL ? builtin->kind : (custom != G_TYPE_INVALID ? kind_of_gtype (custom)
	                                                                  : lrg_bt_asset_node_get_kind (node));

	/* structure */
	if (kind == LRG_BT_NODE_KIND_DECORATOR && node->children->len != 1)
	{
		g_autofree gchar *m = g_strdup_printf ("decorator '%s' needs exactly one child", node->type_name);

		bfail (b, CODE_INVALID, label, m);
		return NULL;
	}
	if (kind == LRG_BT_NODE_KIND_LEAF && node->children->len > 0)
	{
		g_autofree gchar *m = g_strdup_printf ("leaf '%s' has no children", node->type_name);

		bfail (b, CODE_INVALID, label, m);
		return NULL;
	}

	if (b->records != NULL)
	{
		record = g_new0 (Record, 1);
		record->id = g_strdup_printf ("%s%s", prefix, node->id);
		g_ptr_array_add (b->records, record);
	}

	/* children first: the parent is created around them */
	kids = g_ptr_array_new_with_free_func (g_object_unref);
	for (i = 0; i < node->children->len; i++)
	{
		LrgBTNode *k = build_node (b, asset, g_ptr_array_index (node->children, i), prefix);

		if (k == NULL)
			return NULL;
		g_ptr_array_add (kids, k);
	}

	if (builtin != NULL)
		built = kind == LRG_BT_NODE_KIND_LEAF ? build_leaf (b, asset, node, label) : build_inner (b, node, label, kids);
	else if (custom != G_TYPE_INVALID)
	{
		built = g_object_new (custom, NULL);
		if (!apply_custom_props (b, G_OBJECT (built), node, label))
			return NULL;
		for (i = 0; i < kids->len; i++)
		{
			if (LRG_IS_BT_COMPOSITE (built))
				lrg_bt_composite_add_child (LRG_BT_COMPOSITE (built), g_ptr_array_index (kids, i));
			else if (LRG_IS_BT_DECORATOR (built))
				lrg_bt_decorator_set_child (LRG_BT_DECORATOR (built), g_ptr_array_index (kids, i));
		}
	}
	else
	{
		/* an unknown type: a failing leaf when lenient */
		if (!note_missing (b, "type", node->type_name, label))
			return NULL;
		{
			Leaf *leaf = g_new0 (Leaf, 1);

			leaf->kind = LEAF_UNBOUND;
			leaf->label = g_strdup (label);
			leaf->params = lrg_ai_priv_empty_dict ();
			built = LRG_BT_NODE (lrg_bt_action_new (leaf_act, leaf, leaf_free));
		}
	}
	if (built == NULL)
		return NULL;

	lrg_bt_node_set_name (built, node->name != NULL ? node->name : node->id);
	if (record != NULL)
		record->node = g_object_ref (built);
	return g_steal_pointer (&built);
}

/* builds @self with the given mode; the root's built node, or NULL */
static LrgBTNode *
build_with (LrgBtAsset     *self,
            LrgScripting   *scripting,
            LrgBlackboard  *blackboard,
            GPtrArray      *records,
            GPtrArray      *missing,
            gboolean        lenient,
            GError        **error)
{
	g_autoptr(GPtrArray) stack = g_ptr_array_new_with_free_func (g_free);
	Build                b = { self, scripting, blackboard, records, missing, stack, lenient, error };

	if (self->root == NULL)
	{
		g_set_error_literal (error, LRG_BT_ASSET_ERROR, LRG_BT_ASSET_ERROR_INVALID, "root: the tree has no root");
		return NULL;
	}
	ensure_ids (self);
	return build_node (&b, self, self->root, "");
}

gboolean
lrg_bt_asset_validate (LrgBtAsset  *self,
                       GError     **error)
{
	g_autoptr(LrgBTNode) root = NULL;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), FALSE);

	root = build_with (self, NULL, NULL, NULL, NULL, TRUE, error);
	return root != NULL;
}

gchar **
lrg_bt_asset_dup_unbound (LrgBtAsset   *self,
                          LrgScripting *scripting)
{
	g_autoptr(GPtrArray) missing = NULL;
	g_autoptr(LrgBTNode) root = NULL;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);
	g_return_val_if_fail (scripting == NULL || LRG_IS_SCRIPTING (scripting), NULL);

	missing = g_ptr_array_new_with_free_func (g_free);
	root = build_with (self, scripting, NULL, NULL, missing, TRUE, NULL);
	g_ptr_array_add (missing, NULL);
	return (gchar **) g_ptr_array_free (g_steal_pointer (&missing), FALSE);
}

LrgBehaviorTree *
lrg_bt_asset_build (LrgBtAsset     *self,
                    LrgScripting   *scripting,
                    LrgBlackboard  *blackboard,
                    GError        **error)
{
	g_autoptr(LrgBTNode)       root = NULL;
	g_autoptr(LrgBehaviorTree) tree = NULL;

	g_return_val_if_fail (LRG_IS_BT_ASSET (self), NULL);
	g_return_val_if_fail (scripting == NULL || LRG_IS_SCRIPTING (scripting), NULL);
	g_return_val_if_fail (blackboard == NULL || LRG_IS_BLACKBOARD (blackboard), NULL);

	tree = lrg_behavior_tree_new ();
	if (blackboard != NULL)
		lrg_behavior_tree_set_blackboard (tree, blackboard);
	root = build_with (self, scripting, lrg_behavior_tree_get_blackboard (tree), NULL, NULL,
	                   self->allow_unbound, error);
	if (root == NULL)
		return NULL;
	lrg_blackboard_schema_apply_defaults (self->schema, lrg_behavior_tree_get_blackboard (tree), FALSE);
	lrg_behavior_tree_set_root (tree, root);
	return g_steal_pointer (&tree);
}

/* ==========================================================================
 * 7. LrgBtRunner
 * ========================================================================== */

struct _LrgBtRunner
{
	GObject          parent_instance;

	LrgBtAsset      *asset;
	LrgBehaviorTree *tree;
	GPtrArray       *records;    /* Record, depth-first */
	GHashTable      *by_id;      /* id -> Record (not owned) */
	guint64          ticks;
	LrgBTStatus      status;
	const gchar     *current;    /* a record's id, or NULL */
};

enum
{
	RUNNER_SIGNAL_TICKED,
	RUNNER_N_SIGNALS
};

static guint runner_signals[RUNNER_N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgBtRunner, lrg_bt_runner, G_TYPE_OBJECT)

static void
lrg_bt_runner_finalize (GObject *object)
{
	LrgBtRunner *self = LRG_BT_RUNNER (object);

	g_clear_pointer (&self->by_id, g_hash_table_unref);
	g_clear_pointer (&self->records, g_ptr_array_unref);
	g_clear_object (&self->tree);
	g_clear_object (&self->asset);

	G_OBJECT_CLASS (lrg_bt_runner_parent_class)->finalize (object);
}

static void
lrg_bt_runner_class_init (LrgBtRunnerClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_bt_runner_finalize;

	/**
	 * LrgBtRunner::ticked:
	 * @self: the runner
	 * @tick: the tick number (1 for the first)
	 * @status: the root's status
	 *
	 * Emitted after every lrg_bt_runner_tick(), once the debug state is
	 * recorded: the hook a live tree view redraws from.
	 *
	 * Since: 0.9.0
	 */
	runner_signals[RUNNER_SIGNAL_TICKED] =
		g_signal_new ("ticked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_UINT64, LRG_TYPE_BT_STATUS);
}

static void
lrg_bt_runner_init (LrgBtRunner *self)
{
	self->records = g_ptr_array_new_with_free_func (record_free);
	self->by_id = g_hash_table_new (g_str_hash, g_str_equal);
	self->status = LRG_BT_STATUS_INVALID;
}

LrgBtRunner *
lrg_bt_runner_new (LrgBtAsset     *asset,
                   LrgScripting   *scripting,
                   LrgBlackboard  *blackboard,
                   GError        **error)
{
	g_autoptr(LrgBtRunner) self = NULL;
	g_autoptr(LrgBTNode)   root = NULL;
	guint                  i;

	g_return_val_if_fail (LRG_IS_BT_ASSET (asset), NULL);
	g_return_val_if_fail (scripting == NULL || LRG_IS_SCRIPTING (scripting), NULL);
	g_return_val_if_fail (blackboard == NULL || LRG_IS_BLACKBOARD (blackboard), NULL);

	self = g_object_new (LRG_TYPE_BT_RUNNER, NULL);
	self->asset = g_object_ref (asset);
	self->tree = lrg_behavior_tree_new ();
	if (blackboard != NULL)
		lrg_behavior_tree_set_blackboard (self->tree, blackboard);
	root = build_with (asset, scripting, lrg_behavior_tree_get_blackboard (self->tree), self->records, NULL,
	                   asset->allow_unbound, error);
	if (root == NULL)
		return NULL;
	lrg_blackboard_schema_apply_defaults (asset->schema, lrg_behavior_tree_get_blackboard (self->tree), FALSE);
	lrg_behavior_tree_set_root (self->tree, root);

	/* the id index (a real node wins over an alias of the same id) */
	for (i = 0; i < self->records->len; i++)
	{
		Record *r = g_ptr_array_index (self->records, i);

		if (r->node != NULL && (!g_hash_table_contains (self->by_id, r->id) || !r->alias))
			g_hash_table_insert (self->by_id, r->id, r);
		if (r->node != NULL)
			r->ticks = lrg_bt_node_get_tick_count (r->node);
	}
	return g_steal_pointer (&self);
}

LrgBtAsset *
lrg_bt_runner_get_asset (LrgBtRunner *self)
{
	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), NULL);
	return self->asset;
}

LrgBehaviorTree *
lrg_bt_runner_get_tree (LrgBtRunner *self)
{
	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), NULL);
	return self->tree;
}

LrgBlackboard *
lrg_bt_runner_get_blackboard (LrgBtRunner *self)
{
	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), NULL);
	return lrg_behavior_tree_get_blackboard (self->tree);
}

LrgBTStatus
lrg_bt_runner_tick (LrgBtRunner *self,
                    gfloat       delta_time)
{
	const gchar *last_visited = NULL;
	const gchar *deepest_running = NULL;
	guint        i;

	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), LRG_BT_STATUS_INVALID);

	self->status = lrg_behavior_tree_tick (self->tree, delta_time);
	self->ticks++;

	/* a node whose tick count moved was visited by this tick; records are
	 * depth-first, so the last running one is the deepest */
	for (i = 0; i < self->records->len; i++)
	{
		Record *r = g_ptr_array_index (self->records, i);
		guint64 now;

		if (r->node == NULL || r->alias)
			continue;
		now = lrg_bt_node_get_tick_count (r->node);
		r->visited = now != r->ticks;
		r->ticks = now;
		if (!r->visited)
			continue;
		last_visited = r->id;
		if (lrg_bt_node_get_status (r->node) == LRG_BT_STATUS_RUNNING)
			deepest_running = r->id;
	}
	self->current = deepest_running != NULL ? deepest_running : last_visited;

	g_signal_emit (self, runner_signals[RUNNER_SIGNAL_TICKED], 0, self->ticks, self->status);
	return self->status;
}

void
lrg_bt_runner_reset (LrgBtRunner *self)
{
	guint i;

	g_return_if_fail (LRG_IS_BT_RUNNER (self));

	lrg_behavior_tree_abort (self->tree);
	lrg_behavior_tree_reset (self->tree);
	self->status = LRG_BT_STATUS_INVALID;
	self->current = NULL;
	for (i = 0; i < self->records->len; i++)
		((Record *) g_ptr_array_index (self->records, i))->visited = FALSE;
}

LrgBTStatus
lrg_bt_runner_get_status (LrgBtRunner *self)
{
	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), LRG_BT_STATUS_INVALID);
	return self->status;
}

guint64
lrg_bt_runner_get_tick_count (LrgBtRunner *self)
{
	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), 0);
	return self->ticks;
}

guint
lrg_bt_runner_get_node_count (LrgBtRunner *self)
{
	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), 0);
	return self->records->len;
}

const gchar *
lrg_bt_runner_get_node_id (LrgBtRunner *self,
                           guint        index)
{
	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), NULL);

	if (index >= self->records->len)
		return NULL;
	return ((Record *) g_ptr_array_index (self->records, index))->id;
}

LrgBTNode *
lrg_bt_runner_get_node (LrgBtRunner *self,
                        const gchar *id)
{
	Record *r;

	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), NULL);

	r = id != NULL ? g_hash_table_lookup (self->by_id, id) : NULL;
	return r != NULL ? r->node : NULL;
}

LrgBTStatus
lrg_bt_runner_get_node_status (LrgBtRunner *self,
                               const gchar *id)
{
	LrgBTNode *node = lrg_bt_runner_get_node (self, id);

	return node != NULL ? lrg_bt_node_get_status (node) : LRG_BT_STATUS_INVALID;
}

gboolean
lrg_bt_runner_get_node_visited (LrgBtRunner *self,
                                const gchar *id)
{
	Record *r;
	guint   i;

	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), FALSE);

	r = id != NULL ? g_hash_table_lookup (self->by_id, id) : NULL;
	if (r == NULL)
		return FALSE;
	if (!r->alias)
		return r->visited;
	/* an alias is visited when the node it names is */
	for (i = 0; i < self->records->len; i++)
	{
		Record *o = g_ptr_array_index (self->records, i);

		if (!o->alias && o->node == r->node)
			return o->visited;
	}
	return FALSE;
}

gchar **
lrg_bt_runner_dup_active_path (LrgBtRunner *self)
{
	GPtrArray *out;
	guint      i;

	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), NULL);

	out = g_ptr_array_new ();
	for (i = 0; i < self->records->len; i++)
	{
		Record *r = g_ptr_array_index (self->records, i);

		if (r->alias || r->node == NULL || !r->visited)
			continue;
		if (lrg_bt_node_get_status (r->node) == LRG_BT_STATUS_RUNNING)
			g_ptr_array_add (out, g_strdup (r->id));
	}
	g_ptr_array_add (out, NULL);
	return (gchar **) g_ptr_array_free (out, FALSE);
}

const gchar *
lrg_bt_runner_get_current_node (LrgBtRunner *self)
{
	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), NULL);
	return self->current;
}

gchar *
lrg_bt_runner_dup_debug_state (LrgBtRunner *self)
{
	GString *out;
	guint    i;

	g_return_val_if_fail (LRG_IS_BT_RUNNER (self), NULL);

	out = g_string_new (NULL);
	g_string_append_printf (out, "tick: %" G_GUINT64_FORMAT "\nstatus: %s\ncurrent: %s\nnodes:", self->ticks,
	                        lrg_bt_status_to_string (self->status), self->current != NULL ? self->current : "~");
	if (self->records->len == 0)
		g_string_append (out, " []");
	for (i = 0; i < self->records->len; i++)
	{
		Record *r = g_ptr_array_index (self->records, i);

		if (r->alias || r->node == NULL)
			continue;
		g_string_append_printf (out, "\n- {id: \"%s\", status: %s, visited: %s, ticks: %" G_GUINT64_FORMAT "}", r->id,
		                        lrg_bt_status_to_string (lrg_bt_node_get_status (r->node)),
		                        r->visited ? "true" : "false", r->ticks);
	}
	g_string_append_c (out, '\n');
	return g_string_free (out, FALSE);
}
