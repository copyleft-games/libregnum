/* lrg-post-stack.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPostStack: the `.rpost` post-processing look.
 *
 *   version: 1
 *   name: Dusk
 *   quality: high              # low | medium | high | ultra
 *   hdr: true
 *   effects:
 *     - type: bloom            # registered name (or a GType name)
 *       enabled: true
 *       weight: 1.0            # base intensity, 0..1 (blending fades it)
 *       priority: 10           # optional; default index * 10
 *       params:                # effect properties, kebab or snake case
 *         threshold: 0.9
 *         tint: [1.0, 0.9, 0.8]   # expands to tint-r, tint-g, tint-b
 *
 * Every parameter is checked against the effect class when it is read
 * (unknown names and bad values are errors) and stored as a GValue of the
 * property's type, in file order. Keys this version does not know (top
 * level and per effect) are kept as YAML nodes and written back.
 *
 * Special parameters (not properties): palettize `palette` (a list of
 * "#rrggbb" strings), color-grade `lut` (a .cube path, relative to the
 * .rpost) and `lut-strength`.
 */

#include "config.h"
#include "lrg-post-stack.h"
#include "effects/lrg-bloom.h"
#include "effects/lrg-color-grade.h"
#include "effects/lrg-colorblind-filter.h"
#include "effects/lrg-film-grain.h"
#include "effects/lrg-fxaa.h"
#include "effects/lrg-heat-haze.h"
#include "effects/lrg-palettize.h"
#include "effects/lrg-screen-shake.h"
#include "effects/lrg-sharpen.h"
#include "effects/lrg-tonemap.h"
#include "effects/lrg-vignette.h"
#include "lrg-color-lut.h"
#include <math.h>
#include <string.h>
#include <yaml-glib.h>

/* ==========================================================================
 * Data model
 * ========================================================================== */

typedef struct
{
	gchar  *name;           /* canonical (kebab-case) parameter name */
	GValue  value;          /* of the property's value type */
} StackParam;

typedef struct
{
	gchar       *type_name;
	GType        gtype;
	gboolean     enabled;
	gfloat       weight;
	gboolean     has_priority;
	gint         priority;
	GPtrArray   *params;    /* StackParam, in file order */
	YamlMapping *extra;     /* unknown keys, nullable */
} StackEntry;

struct _LrgPostStack
{
	GObject         parent_instance;

	gchar          *name;
	LrgPostQuality  quality;
	gboolean        hdr;
	GPtrArray      *entries;   /* StackEntry */
	YamlMapping    *extra;     /* unknown top-level keys, nullable */
	gchar          *base_dir;  /* relative paths (LUTs) resolve here */
};

G_DEFINE_FINAL_TYPE (LrgPostStack, lrg_post_stack, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-post-stack-error-quark, lrg_post_stack_error)

GType
lrg_post_stack_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_POST_STACK_ERROR_PARSE, "LRG_POST_STACK_ERROR_PARSE", "parse" },
			{ LRG_POST_STACK_ERROR_UNKNOWN_EFFECT, "LRG_POST_STACK_ERROR_UNKNOWN_EFFECT", "unknown-effect" },
			{ LRG_POST_STACK_ERROR_UNKNOWN_PARAM, "LRG_POST_STACK_ERROR_UNKNOWN_PARAM", "unknown-param" },
			{ LRG_POST_STACK_ERROR_INVALID_VALUE, "LRG_POST_STACK_ERROR_INVALID_VALUE", "invalid-value" },
			{ LRG_POST_STACK_ERROR_IO, "LRG_POST_STACK_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgPostStackError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

static void
stack_param_free (gpointer data)
{
	StackParam *p = data;

	g_free (p->name);
	g_value_unset (&p->value);
	g_free (p);
}

static StackParam *
stack_param_new (const gchar  *name,
                 const GValue *value)
{
	StackParam *p = g_new0 (StackParam, 1);

	p->name = g_strdup (name);
	g_value_init (&p->value, G_VALUE_TYPE (value));
	g_value_copy (value, &p->value);
	return p;
}

static void
stack_entry_free (gpointer data)
{
	StackEntry *e = data;

	g_free (e->type_name);
	g_ptr_array_unref (e->params);
	if (e->extra != NULL)
		yaml_mapping_unref (e->extra);
	g_free (e);
}

static StackEntry *
stack_entry_new (const gchar *type_name,
                 GType        gtype)
{
	StackEntry *e = g_new0 (StackEntry, 1);

	e->type_name = g_strdup (type_name);
	e->gtype = gtype;
	e->enabled = TRUE;
	e->weight = 1.0f;
	e->params = g_ptr_array_new_with_free_func (stack_param_free);
	return e;
}

static StackEntry *
stack_entry_copy (const StackEntry *src)
{
	StackEntry *e = stack_entry_new (src->type_name, src->gtype);
	guint       i;

	e->enabled = src->enabled;
	e->weight = src->weight;
	e->has_priority = src->has_priority;
	e->priority = src->priority;
	for (i = 0; i < src->params->len; i++)
	{
		StackParam *p = g_ptr_array_index (src->params, i);

		g_ptr_array_add (e->params, stack_param_new (p->name, &p->value));
	}
	if (src->extra != NULL)
		e->extra = yaml_mapping_ref (src->extra);
	return e;
}

static StackParam *
entry_find_param (StackEntry  *e,
                  const gchar *name)
{
	guint i;

	for (i = 0; i < e->params->len; i++)
	{
		StackParam *p = g_ptr_array_index (e->params, i);

		if (g_strcmp0 (p->name, name) == 0)
			return p;
	}
	return NULL;
}

/* Sets (replacing in place, keeping the order) */
static void
entry_set_param (StackEntry   *e,
                 const gchar  *name,
                 const GValue *value)
{
	StackParam *p = entry_find_param (e, name);

	if (p != NULL)
	{
		g_value_unset (&p->value);
		g_value_init (&p->value, G_VALUE_TYPE (value));
		g_value_copy (value, &p->value);
		return;
	}
	g_ptr_array_add (e->params, stack_param_new (name, value));
}

/* ==========================================================================
 * Effect type registry
 * ========================================================================== */

static GMutex      registry_lock;
static GHashTable *registry;   /* name -> GType (as pointer) */

static void
ensure_registry_locked (void)
{
	if (registry != NULL)
		return;
	registry = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
#define REG(n, t) g_hash_table_insert (registry, g_strdup (n), GSIZE_TO_POINTER ((gsize) (t)))
	REG ("bloom", LRG_TYPE_BLOOM);
	REG ("color-grade", LRG_TYPE_COLOR_GRADE);
	REG ("colorblind-filter", LRG_TYPE_COLORBLIND_FILTER);
	REG ("film-grain", LRG_TYPE_FILM_GRAIN);
	REG ("fxaa", LRG_TYPE_FXAA);
	REG ("heat-haze", LRG_TYPE_HEAT_HAZE);
	REG ("palettize", LRG_TYPE_PALETTIZE);
	REG ("screen-shake", LRG_TYPE_SCREEN_SHAKE);
	REG ("sharpen", LRG_TYPE_SHARPEN);
	REG ("tonemap", LRG_TYPE_TONEMAP);
	REG ("vignette", LRG_TYPE_VIGNETTE);
#undef REG
}

void
lrg_post_stack_register_effect_type (const gchar *name,
                                     GType        type)
{
	g_return_if_fail (name != NULL && *name != '\0');
	g_return_if_fail (g_type_is_a (type, LRG_TYPE_POST_EFFECT));
	g_return_if_fail (!G_TYPE_IS_ABSTRACT (type));

	g_mutex_lock (&registry_lock);
	ensure_registry_locked ();
	g_hash_table_insert (registry, g_strdup (name), GSIZE_TO_POINTER ((gsize) type));
	g_mutex_unlock (&registry_lock);
}

GType
lrg_post_stack_lookup_effect_type (const gchar *name)
{
	gpointer found = NULL;
	gboolean hit;
	GType    t;

	g_return_val_if_fail (name != NULL, G_TYPE_INVALID);

	g_mutex_lock (&registry_lock);
	ensure_registry_locked ();
	hit = g_hash_table_lookup_extended (registry, name, NULL, &found);
	g_mutex_unlock (&registry_lock);
	if (hit)
		return (GType) GPOINTER_TO_SIZE (found);

	/* A GType name of a concrete effect type */
	t = g_type_from_name (name);
	if (t != G_TYPE_INVALID && g_type_is_a (t, LRG_TYPE_POST_EFFECT) && !G_TYPE_IS_ABSTRACT (t))
		return t;
	return G_TYPE_INVALID;
}

gchar **
lrg_post_stack_list_effect_types (void)
{
	g_autoptr(GPtrArray) names = g_ptr_array_new ();
	GHashTableIter       iter;
	gpointer             key;

	g_mutex_lock (&registry_lock);
	ensure_registry_locked ();
	g_hash_table_iter_init (&iter, registry);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (names, g_strdup (key));
	g_mutex_unlock (&registry_lock);

	g_ptr_array_sort_values (names, (GCompareFunc) g_strcmp0);
	g_ptr_array_add (names, NULL);
	return (gchar **) g_ptr_array_free (g_steal_pointer (&names), FALSE);
}

/* The registered name of @type (first in sorted order), or NULL */
static gchar *
type_to_name (GType type)
{
	g_auto(GStrv) names = lrg_post_stack_list_effect_types ();
	guint         i;

	for (i = 0; names[i] != NULL; i++)
		if (lrg_post_stack_lookup_effect_type (names[i]) == type)
			return g_strdup (names[i]);
	return NULL;
}

/* ==========================================================================
 * Parameters: names, types, conversions
 * ========================================================================== */

/* kebab-case copy of @name */
static gchar *
canonical_name (const gchar *name)
{
	gchar *c = g_strdup (name);

	g_strdelimit (c, "_", '-');
	return c;
}

/* The value type of a special (non-property) parameter, else invalid */
static GType
special_param_type (GType        effect_type,
                    const gchar *name)
{
	if (g_type_is_a (effect_type, LRG_TYPE_PALETTIZE) && g_strcmp0 (name, "palette") == 0)
		return G_TYPE_STRV;
	if (g_type_is_a (effect_type, LRG_TYPE_COLOR_GRADE))
	{
		if (g_strcmp0 (name, "lut") == 0)
			return G_TYPE_STRING;
		if (g_strcmp0 (name, "lut-strength") == 0)
			return G_TYPE_FLOAT;
	}
	return G_TYPE_INVALID;
}

/* A settable effect property (not one of the base class's: those are the
 * entry keys enabled/weight/priority and the stack's quality) */
static gboolean
is_param_pspec (GParamSpec *pspec)
{
	return pspec != NULL && (pspec->flags & G_PARAM_WRITABLE) != 0 &&
	       (pspec->flags & G_PARAM_READABLE) != 0 &&
	       (pspec->flags & G_PARAM_CONSTRUCT_ONLY) == 0 &&
	       pspec->owner_type != LRG_TYPE_POST_EFFECT && pspec->owner_type != G_TYPE_OBJECT;
}

static GParamSpec *
find_param_pspec (GType        effect_type,
                  const gchar *name)
{
	g_autoptr(GTypeClass) klass = g_type_class_ref (effect_type);
	GParamSpec           *pspec = g_object_class_find_property (G_OBJECT_CLASS (klass), name);

	return is_param_pspec (pspec) ? pspec : NULL;
}

/* "#rrggbb" or "rrggbb" -> TRUE */
static gboolean
parse_hex_rgb (const gchar *s,
               guint8      *rgb)
{
	gint i;

	if (s == NULL)
		return FALSE;
	if (*s == '#')
		s++;
	if (strlen (s) != 6)
		return FALSE;
	for (i = 0; i < 3; i++)
	{
		gint hi = g_ascii_xdigit_value (s[i * 2]);
		gint lo = g_ascii_xdigit_value (s[i * 2 + 1]);

		if (hi < 0 || lo < 0)
			return FALSE;
		rgb[i] = (guint8) (hi * 16 + lo);
	}
	return TRUE;
}

/* Parses a scalar string into @out (initialised to @type). */
static gboolean
parse_scalar (const gchar  *text,
              GType         type,
              GParamSpec   *pspec,
              const gchar  *what,
              GValue       *out,
              GError      **error)
{
	GType  fund = G_TYPE_FUNDAMENTAL (type);
	gchar *end = NULL;
	gdouble d;

	g_value_init (out, type);
	if (text == NULL)
		text = "";
	switch (fund)
	{
	case G_TYPE_BOOLEAN:
		if (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "yes") == 0 ||
		    g_ascii_strcasecmp (text, "on") == 0 || g_strcmp0 (text, "1") == 0)
			g_value_set_boolean (out, TRUE);
		else if (g_ascii_strcasecmp (text, "false") == 0 || g_ascii_strcasecmp (text, "no") == 0 ||
		         g_ascii_strcasecmp (text, "off") == 0 || g_strcmp0 (text, "0") == 0)
			g_value_set_boolean (out, FALSE);
		else
			goto bad;
		return TRUE;
	case G_TYPE_STRING:
		g_value_set_string (out, text);
		return TRUE;
	case G_TYPE_ENUM:
	{
		g_autoptr(GEnumClass) eclass = g_type_class_ref (type);
		GEnumValue           *ev = g_enum_get_value_by_nick (eclass, text);

		if (ev == NULL)
			ev = g_enum_get_value_by_name (eclass, text);
		if (ev == NULL)
		{
			gint64 n = g_ascii_strtoll (text, &end, 10);

			if (end != text && *end == '\0')
				ev = g_enum_get_value (eclass, (gint) n);
		}
		if (ev == NULL)
			goto bad;
		g_value_set_enum (out, ev->value);
		break;
	}
	case G_TYPE_INT:
	case G_TYPE_UINT:
	case G_TYPE_INT64:
	case G_TYPE_UINT64:
	case G_TYPE_LONG:
	case G_TYPE_ULONG:
	case G_TYPE_FLOAT:
	case G_TYPE_DOUBLE:
		d = g_ascii_strtod (text, &end);
		if (end == text || *end != '\0' || !isfinite (d))
			goto bad;
		if (fund == G_TYPE_FLOAT)
			g_value_set_float (out, (gfloat) d);
		else if (fund == G_TYPE_DOUBLE)
			g_value_set_double (out, d);
		else if (fund == G_TYPE_INT)
			g_value_set_int (out, (gint) CLAMP (round (d), (gdouble) G_MININT, (gdouble) G_MAXINT));
		else if (fund == G_TYPE_UINT)
			g_value_set_uint (out, (guint) CLAMP (round (d), 0.0, (gdouble) G_MAXUINT));
		else if (fund == G_TYPE_INT64)
			g_value_set_int64 (out, (gint64) round (d));
		else if (fund == G_TYPE_UINT64)
			g_value_set_uint64 (out, (guint64) MAX (round (d), 0.0));
		else if (fund == G_TYPE_LONG)
			g_value_set_long (out, (glong) round (d));
		else
			g_value_set_ulong (out, (gulong) MAX (round (d), 0.0));
		break;
	default:
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE,
		             "%s: values of type %s cannot be written in a post stack", what,
		             g_type_name (type));
		g_value_unset (out);
		return FALSE;
	}
	/* out-of-range numbers clamp to the property range */
	if (pspec != NULL)
		g_param_value_validate (pspec, out);
	return TRUE;

bad:
	g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE,
	             "%s: '%s' is not a valid %s", what, text, g_type_name (type));
	g_value_unset (out);
	return FALSE;
}

/* Converts a caller's GValue to the parameter's type */
static gboolean
convert_value (const GValue  *in,
               GType          type,
               GParamSpec    *pspec,
               const gchar   *what,
               GValue        *out,
               GError       **error)
{
	if (G_VALUE_HOLDS_STRING (in) && type != G_TYPE_STRING)
		return parse_scalar (g_value_get_string (in), type, pspec, what, out, error);
	g_value_init (out, type);
	if (G_VALUE_TYPE (in) == type)
		g_value_copy (in, out);
	else if (!g_value_type_transformable (G_VALUE_TYPE (in), type) || !g_value_transform (in, out))
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE,
		             "%s: cannot convert %s to %s", what, G_VALUE_TYPE_NAME (in), g_type_name (type));
		g_value_unset (out);
		return FALSE;
	}
	if (pspec != NULL)
		g_param_value_validate (pspec, out);
	if (type == G_TYPE_STRV)
	{
		const gchar * const *v = g_value_get_boxed (out);
		guint8               rgb[3];
		gint                 i;

		for (i = 0; v != NULL && v[i] != NULL; i++)
			if (!parse_hex_rgb (v[i], rgb))
			{
				g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE,
				             "%s: '%s' is not a #rrggbb colour", what, v[i]);
				g_value_unset (out);
				return FALSE;
			}
	}
	return TRUE;
}

/*
 * Resolves a parameter name for @entry: its value type and pspec (NULL
 * for special keys). FALSE with UNKNOWN_PARAM.
 */
static gboolean
resolve_param (StackEntry   *entry,
               const gchar  *name,
               GType        *type,
               GParamSpec  **pspec,
               GError      **error)
{
	*pspec = find_param_pspec (entry->gtype, name);
	if (*pspec != NULL)
	{
		*type = G_PARAM_SPEC_VALUE_TYPE (*pspec);
		return TRUE;
	}
	*type = special_param_type (entry->gtype, name);
	if (*type != G_TYPE_INVALID)
		return TRUE;
	g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_UNKNOWN_PARAM,
	             "effect '%s' has no parameter '%s'", entry->type_name, name);
	return FALSE;
}

/* ==========================================================================
 * YAML in
 * ========================================================================== */

static const gchar *
scalar_of (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

/* An absent or null value (`~`, `null`, empty) */
static gboolean
is_empty_node (YamlNode *node)
{
	const gchar *s;

	if (node == NULL || yaml_node_is_null (node))
		return TRUE;
	s = scalar_of (node);
	return s != NULL && (s[0] == '\0' || g_strcmp0 (s, "~") == 0 || g_strcmp0 (s, "null") == 0);
}

/* One `params:` member: scalar, list of colours, or [r, g, b(, a)] */
static gboolean
read_param (StackEntry   *entry,
            const gchar  *key,
            YamlNode     *node,
            GError      **error)
{
	g_autofree gchar *name = canonical_name (key);
	g_autofree gchar *what = g_strdup_printf ("%s.%s", entry->type_name, name);
	GParamSpec       *pspec = NULL;
	GType             type = G_TYPE_INVALID;

	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (node);
		guint         n = yaml_sequence_get_length (seq);
		guint         i;

		/* A palette: a list of "#rrggbb" */
		if (special_param_type (entry->gtype, name) == G_TYPE_STRV)
		{
			g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
			g_auto(GStrv)           v = NULL;
			g_auto(GValue)          strv = G_VALUE_INIT;
			guint8                  rgb[3];

			for (i = 0; i < n; i++)
			{
				const gchar *s = scalar_of (yaml_sequence_get_element (seq, i));

				if (!parse_hex_rgb (s, rgb))
				{
					g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE,
					             "%s: '%s' is not a #rrggbb colour", what, s != NULL ? s : "");
					return FALSE;
				}
				g_strv_builder_add (b, s);
			}
			v = g_strv_builder_end (b);
			g_value_init (&strv, G_TYPE_STRV);
			g_value_set_boxed (&strv, v);
			entry_set_param (entry, name, &strv);
			return TRUE;
		}

		/* A vector: name-r, name-g, name-b(, name-a) */
		if (n < 3 || n > 4)
		{
			g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE,
			             "%s: a list must have 3 or 4 numbers ([r, g, b] or [r, g, b, a])", what);
			return FALSE;
		}
		for (i = 0; i < n; i++)
		{
			static const gchar *suffix[] = { "r", "g", "b", "a" };
			g_autofree gchar   *sub = g_strdup_printf ("%s-%s", name, suffix[i]);
			g_autofree gchar   *subwhat = g_strdup_printf ("%s.%s", entry->type_name, sub);
			g_auto(GValue)      v = G_VALUE_INIT;

			if (!resolve_param (entry, sub, &type, &pspec, error))
				return FALSE;
			if (!parse_scalar (scalar_of (yaml_sequence_get_element (seq, i)), type, pspec, subwhat, &v,
			                   error))
				return FALSE;
			entry_set_param (entry, sub, &v);
		}
		return TRUE;
	}

	{
		g_auto(GValue) v = G_VALUE_INIT;

		if (!resolve_param (entry, name, &type, &pspec, error))
			return FALSE;
		if (type == G_TYPE_STRV)
		{
			g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE,
			             "%s: expected a list of \"#rrggbb\" colours", what);
			return FALSE;
		}
		if (scalar_of (node) == NULL)
		{
			g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_INVALID_VALUE,
			             "%s: expected a single value", what);
			return FALSE;
		}
		if (!parse_scalar (scalar_of (node), type, pspec, what, &v, error))
			return FALSE;
		entry_set_param (entry, name, &v);
	}
	return TRUE;
}

static StackEntry *
read_entry (YamlNode  *node,
            guint      index,
            GError   **error)
{
	YamlMapping       *map;
	StackEntry        *entry;
	g_autoptr(GList)   keys = NULL;
	GList             *l;
	const gchar       *type_name;
	GType              gtype;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_PARSE,
		             "effects[%u]: expected a mapping", index);
		return NULL;
	}
	map = yaml_node_get_mapping (node);
	type_name = scalar_of (yaml_mapping_get_member (map, "type"));
	if (type_name == NULL || *type_name == '\0')
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_PARSE,
		             "effects[%u]: missing 'type'", index);
		return NULL;
	}
	gtype = lrg_post_stack_lookup_effect_type (type_name);
	if (gtype == G_TYPE_INVALID)
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_UNKNOWN_EFFECT,
		             "effects[%u]: unknown effect type '%s'", index, type_name);
		return NULL;
	}

	entry = stack_entry_new (type_name, gtype);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar *key = l->data;
		YamlNode    *member = yaml_mapping_get_member (map, key);
		g_auto(GValue) v = G_VALUE_INIT;

		if (g_strcmp0 (key, "type") == 0)
			continue;
		if (g_strcmp0 (key, "enabled") == 0 || g_strcmp0 (key, "weight") == 0 ||
		    g_strcmp0 (key, "priority") == 0)
		{
			GType t = key[0] == 'e' ? G_TYPE_BOOLEAN : key[0] == 'w' ? G_TYPE_FLOAT : G_TYPE_INT;
			g_autofree gchar *what = g_strdup_printf ("effects[%u].%s", index, key);

			if (!parse_scalar (scalar_of (member), t, NULL, what, &v, error))
				goto fail;
			if (key[0] == 'e')
				entry->enabled = g_value_get_boolean (&v);
			else if (key[0] == 'w')
				entry->weight = CLAMP (g_value_get_float (&v), 0.0f, 1.0f);
			else
			{
				entry->has_priority = TRUE;
				entry->priority = g_value_get_int (&v);
			}
			continue;
		}
		if (g_strcmp0 (key, "params") == 0)
		{
			YamlMapping      *pmap;
			g_autoptr(GList)  pkeys = NULL;
			GList            *pl;

			if (is_empty_node (member))
				continue;
			if (yaml_node_get_node_type (member) != YAML_NODE_MAPPING)
			{
				g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_PARSE,
				             "effects[%u].params: expected a mapping", index);
				goto fail;
			}
			pmap = yaml_node_get_mapping (member);
			pkeys = yaml_mapping_get_members (pmap);
			for (pl = pkeys; pl != NULL; pl = pl->next)
				if (!read_param (entry, pl->data, yaml_mapping_get_member (pmap, pl->data), error))
					goto fail;
			continue;
		}

		/* Unknown: kept for the round trip */
		if (entry->extra == NULL)
			entry->extra = yaml_mapping_new ();
		yaml_mapping_set_member (entry->extra, key, member);
	}
	return entry;

fail:
	stack_entry_free (entry);
	return NULL;
}

static gboolean
read_stack (LrgPostStack  *self,
            YamlNode      *root,
            GError       **error)
{
	YamlMapping      *map;
	g_autoptr(GList)  keys = NULL;
	GList            *l;
	gboolean          has_effects = FALSE;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_PARSE,
		                     "a post stack must be a YAML mapping");
		return FALSE;
	}
	map = yaml_node_get_mapping (root);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		const gchar   *key = l->data;
		YamlNode      *member = yaml_mapping_get_member (map, key);
		g_auto(GValue) v = G_VALUE_INIT;

		if (g_strcmp0 (key, "version") == 0)
		{
			if (!parse_scalar (scalar_of (member), G_TYPE_INT, NULL, "version", &v, error))
				return FALSE;
			if (g_value_get_int (&v) > LRG_POST_STACK_FORMAT_VERSION)
			{
				g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_PARSE,
				             "version %d is newer than this build reads (%d)", g_value_get_int (&v),
				             LRG_POST_STACK_FORMAT_VERSION);
				return FALSE;
			}
		}
		else if (g_strcmp0 (key, "name") == 0)
			lrg_post_stack_set_name (self, scalar_of (member));
		else if (g_strcmp0 (key, "quality") == 0)
		{
			if (!parse_scalar (scalar_of (member), LRG_TYPE_POST_QUALITY, NULL, "quality", &v, error))
				return FALSE;
			self->quality = (LrgPostQuality) g_value_get_enum (&v);
		}
		else if (g_strcmp0 (key, "hdr") == 0)
		{
			if (!parse_scalar (scalar_of (member), G_TYPE_BOOLEAN, NULL, "hdr", &v, error))
				return FALSE;
			self->hdr = g_value_get_boolean (&v);
		}
		else if (g_strcmp0 (key, "effects") == 0)
		{
			YamlSequence *seq;
			guint         i, n;

			has_effects = TRUE;
			if (is_empty_node (member))
				continue;
			if (yaml_node_get_node_type (member) != YAML_NODE_SEQUENCE)
			{
				g_set_error_literal (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_PARSE,
				                     "'effects' must be a list");
				return FALSE;
			}
			seq = yaml_node_get_sequence (member);
			n = yaml_sequence_get_length (seq);
			for (i = 0; i < n; i++)
			{
				StackEntry *e = read_entry (yaml_sequence_get_element (seq, i), i, error);

				if (e == NULL)
					return FALSE;
				g_ptr_array_add (self->entries, e);
			}
		}
		else
		{
			if (self->extra == NULL)
				self->extra = yaml_mapping_new ();
			yaml_mapping_set_member (self->extra, key, member);
		}
	}
	if (!has_effects)
	{
		g_set_error_literal (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_PARSE,
		                     "not a post stack: no 'effects' list");
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * YAML out
 * ========================================================================== */

static YamlNode *
value_to_node (const GValue *v)
{
	GType fund = G_TYPE_FUNDAMENTAL (G_VALUE_TYPE (v));
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	if (G_VALUE_TYPE (v) == G_TYPE_STRV)
	{
		YamlNode            *node = yaml_node_new_sequence (NULL);
		YamlSequence        *seq = yaml_node_get_sequence (node);
		const gchar * const *s = g_value_get_boxed (v);
		gint                 i;

		for (i = 0; s != NULL && s[i] != NULL; i++)
			yaml_sequence_add_string_element (seq, s[i]);
		return node;
	}
	switch (fund)
	{
	case G_TYPE_BOOLEAN:
		return yaml_node_new_boolean (g_value_get_boolean (v));
	case G_TYPE_INT:
		return yaml_node_new_int (g_value_get_int (v));
	case G_TYPE_UINT:
		return yaml_node_new_int ((gint64) g_value_get_uint (v));
	case G_TYPE_INT64:
		return yaml_node_new_int (g_value_get_int64 (v));
	case G_TYPE_UINT64:
		return yaml_node_new_int ((gint64) g_value_get_uint64 (v));
	case G_TYPE_LONG:
		return yaml_node_new_int ((gint64) g_value_get_long (v));
	case G_TYPE_ULONG:
		return yaml_node_new_int ((gint64) g_value_get_ulong (v));
	case G_TYPE_FLOAT:
		/* 6 significant digits: what a gfloat holds, locale independent */
		return yaml_node_new_scalar (g_ascii_formatd (buf, sizeof buf, "%.6g", (gdouble) g_value_get_float (v)),
		                             YAML_SCALAR_STYLE_PLAIN);
	case G_TYPE_DOUBLE:
		return yaml_node_new_scalar (g_ascii_formatd (buf, sizeof buf, "%.10g", g_value_get_double (v)),
		                             YAML_SCALAR_STYLE_PLAIN);
	case G_TYPE_ENUM:
	{
		g_autoptr(GEnumClass) eclass = g_type_class_ref (G_VALUE_TYPE (v));
		GEnumValue           *ev = g_enum_get_value (eclass, g_value_get_enum (v));

		if (ev != NULL)
			return yaml_node_new_string (ev->value_nick);
		return yaml_node_new_int (g_value_get_enum (v));
	}
	case G_TYPE_STRING:
	default:
		return yaml_node_new_string (G_VALUE_HOLDS_STRING (v) && g_value_get_string (v) != NULL
		                             ? g_value_get_string (v) : "");
	}
}

/* Copies every member of @extra into @map */
static void
copy_extra (YamlMapping *map,
            YamlMapping *extra)
{
	g_autoptr(GList) keys = NULL;
	GList           *l;

	if (extra == NULL)
		return;
	keys = yaml_mapping_get_members (extra);
	for (l = keys; l != NULL; l = l->next)
		yaml_mapping_set_member (map, l->data, yaml_mapping_get_member (extra, l->data));
}

static void
set_node (YamlMapping *map,
          const gchar *key,
          YamlNode    *node)
{
	yaml_mapping_set_member (map, key, node);
	yaml_node_unref (node);
}

gchar *
lrg_post_stack_to_data (LrgPostStack  *self,
                        GError       **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlNode)      effects = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	g_autoptr(GEnumClass)    qclass = NULL;
	YamlMapping             *map;
	YamlSequence            *seq;
	guint                    i, j;

	g_return_val_if_fail (LRG_IS_POST_STACK (self), NULL);

	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_int_member (map, "version", LRG_POST_STACK_FORMAT_VERSION);
	if (self->name != NULL)
		yaml_mapping_set_string_member (map, "name", self->name);
	qclass = g_type_class_ref (LRG_TYPE_POST_QUALITY);
	yaml_mapping_set_string_member (map, "quality", g_enum_get_value (qclass, (gint) self->quality)->value_nick);
	yaml_mapping_set_boolean_member (map, "hdr", self->hdr);
	copy_extra (map, self->extra);

	effects = yaml_node_new_sequence (NULL);
	seq = yaml_node_get_sequence (effects);
	for (i = 0; i < self->entries->len; i++)
	{
		StackEntry          *e = g_ptr_array_index (self->entries, i);
		g_autoptr(YamlNode)  enode = yaml_node_new_mapping (NULL);
		YamlMapping         *emap = yaml_node_get_mapping (enode);
		GValue               w = G_VALUE_INIT;

		yaml_mapping_set_string_member (emap, "type", e->type_name);
		yaml_mapping_set_boolean_member (emap, "enabled", e->enabled);
		g_value_init (&w, G_TYPE_FLOAT);
		g_value_set_float (&w, e->weight);
		set_node (emap, "weight", value_to_node (&w));
		g_value_unset (&w);
		if (e->has_priority)
			yaml_mapping_set_int_member (emap, "priority", e->priority);
		if (e->params->len > 0)
		{
			g_autoptr(YamlNode) pnode = yaml_node_new_mapping (NULL);
			YamlMapping        *pmap = yaml_node_get_mapping (pnode);

			for (j = 0; j < e->params->len; j++)
			{
				StackParam *p = g_ptr_array_index (e->params, j);

				set_node (pmap, p->name, value_to_node (&p->value));
			}
			yaml_mapping_set_member (emap, "params", pnode);
		}
		copy_extra (emap, e->extra);
		yaml_sequence_add_element (seq, enode);
	}
	yaml_mapping_set_member (map, "effects", effects);

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, NULL, error);
}

gboolean
lrg_post_stack_save_to_file (LrgPostStack  *self,
                             const gchar   *path,
                             GError       **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_POST_STACK (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_post_stack_to_data (self, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, -1, &local))
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_IO,
		             "%s", local->message);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_post_stack_finalize (GObject *object)
{
	LrgPostStack *self = LRG_POST_STACK (object);

	g_free (self->name);
	g_free (self->base_dir);
	g_ptr_array_unref (self->entries);
	if (self->extra != NULL)
		yaml_mapping_unref (self->extra);

	G_OBJECT_CLASS (lrg_post_stack_parent_class)->finalize (object);
}

static void
lrg_post_stack_class_init (LrgPostStackClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_post_stack_finalize;
}

static void
lrg_post_stack_init (LrgPostStack *self)
{
	self->quality = LRG_POST_QUALITY_HIGH;
	self->hdr = TRUE;
	self->entries = g_ptr_array_new_with_free_func (stack_entry_free);
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgPostStack *
lrg_post_stack_new (void)
{
	return g_object_new (LRG_TYPE_POST_STACK, NULL);
}

LrgPostStack *
lrg_post_stack_new_from_data (const gchar  *data,
                              gssize        length,
                              const gchar  *base_dir,
                              GError      **error)
{
	g_autoptr(LrgPostStack) self = NULL;
	g_autoptr(YamlParser)   parser = NULL;
	g_autoptr(GError)       local = NULL;
	YamlNode               *root;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_PARSE, "%s", local->message);
		return NULL;
	}
	root = yaml_parser_get_root (parser);
	self = lrg_post_stack_new ();
	self->base_dir = g_strdup (base_dir);
	if (!read_stack (self, root, error))
		return NULL;
	return g_steal_pointer (&self);
}

LrgPostStack *
lrg_post_stack_new_from_file (const gchar  *path,
                              GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autofree gchar *dir = NULL;
	g_autoptr(GError) local = NULL;
	LrgPostStack     *self;
	gsize             len = 0;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_IO, "%s", local->message);
		return NULL;
	}
	dir = g_path_get_dirname (path);
	self = lrg_post_stack_new_from_data (text, (gssize) len, dir, &local);
	if (self == NULL)
	{
		g_set_error (error, LRG_POST_STACK_ERROR, local->code, "%s: %s", path, local->message);
		return NULL;
	}
	return self;
}

LrgPostStack *
lrg_post_stack_copy (LrgPostStack *self)
{
	LrgPostStack *copy;
	guint         i;

	g_return_val_if_fail (LRG_IS_POST_STACK (self), NULL);

	copy = lrg_post_stack_new ();
	copy->name = g_strdup (self->name);
	copy->quality = self->quality;
	copy->hdr = self->hdr;
	copy->base_dir = g_strdup (self->base_dir);
	if (self->extra != NULL)
		copy->extra = yaml_mapping_ref (self->extra);
	for (i = 0; i < self->entries->len; i++)
		g_ptr_array_add (copy->entries, stack_entry_copy (g_ptr_array_index (self->entries, i)));
	return copy;
}

LrgPostStack *
lrg_post_stack_new_from_processor (LrgPostProcessor *processor)
{
	LrgPostStack *self;
	GList        *l;

	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (processor), NULL);

	self = lrg_post_stack_new ();
	self->quality = lrg_post_processor_get_quality (processor);
	self->hdr = lrg_post_processor_get_hdr (processor);
	for (l = lrg_post_processor_get_effects (processor); l != NULL; l = l->next)
	{
		LrgPostEffect     *effect = LRG_POST_EFFECT (l->data);
		g_autofree gchar  *name = type_to_name (G_OBJECT_TYPE (effect));
		g_autofree GParamSpec **pspecs = NULL;
		StackEntry        *e;
		guint              n = 0, i;

		if (name == NULL)
			continue;
		e = stack_entry_new (name, G_OBJECT_TYPE (effect));
		e->enabled = lrg_post_effect_is_enabled (effect);
		e->weight = lrg_post_effect_get_intensity (effect);

		/* Properties away from their defaults, in declaration order */
		pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (effect), &n);
		for (i = 0; i < n; i++)
		{
			g_auto(GValue) v = G_VALUE_INIT;

			if (!is_param_pspec (pspecs[i]))
				continue;
			g_value_init (&v, G_PARAM_SPEC_VALUE_TYPE (pspecs[i]));
			g_object_get_property (G_OBJECT (effect), pspecs[i]->name, &v);
			if (g_param_value_defaults (pspecs[i], &v))
				continue;
			entry_set_param (e, pspecs[i]->name, &v);
		}

		/* A palette is data, not a property */
		if (LRG_IS_PALETTIZE (effect) && lrg_palettize_get_palette_size (LRG_PALETTIZE (effect)) > 0)
		{
			g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
			g_auto(GStrv)           colours = NULL;
			g_auto(GValue)          v = G_VALUE_INIT;
			guint                   k;

			for (k = 0; k < lrg_palettize_get_palette_size (LRG_PALETTIZE (effect)); k++)
			{
				guint8 r = 0, g = 0, bb = 0;

				lrg_palettize_get_color (LRG_PALETTIZE (effect), k, &r, &g, &bb);
				g_strv_builder_take (b, g_strdup_printf ("#%02x%02x%02x", r, g, bb));
			}
			colours = g_strv_builder_end (b);
			g_value_init (&v, G_TYPE_STRV);
			g_value_set_boxed (&v, colours);
			entry_set_param (e, "palette", &v);
		}
		g_ptr_array_add (self->entries, e);
	}
	return self;
}

/* ==========================================================================
 * Settings and effects
 * ========================================================================== */

const gchar *
lrg_post_stack_get_name (LrgPostStack *self)
{
	g_return_val_if_fail (LRG_IS_POST_STACK (self), NULL);
	return self->name;
}

void
lrg_post_stack_set_name (LrgPostStack *self,
                         const gchar  *name)
{
	g_return_if_fail (LRG_IS_POST_STACK (self));
	g_free (self->name);
	self->name = g_strdup (name);
}

LrgPostQuality
lrg_post_stack_get_quality (LrgPostStack *self)
{
	g_return_val_if_fail (LRG_IS_POST_STACK (self), LRG_POST_QUALITY_HIGH);
	return self->quality;
}

void
lrg_post_stack_set_quality (LrgPostStack   *self,
                            LrgPostQuality  quality)
{
	g_return_if_fail (LRG_IS_POST_STACK (self));
	g_return_if_fail (quality <= LRG_POST_QUALITY_ULTRA);
	self->quality = quality;
}

gboolean
lrg_post_stack_get_hdr (LrgPostStack *self)
{
	g_return_val_if_fail (LRG_IS_POST_STACK (self), TRUE);
	return self->hdr;
}

void
lrg_post_stack_set_hdr (LrgPostStack *self,
                        gboolean      hdr)
{
	g_return_if_fail (LRG_IS_POST_STACK (self));
	self->hdr = !!hdr;
}

guint
lrg_post_stack_get_effect_count (LrgPostStack *self)
{
	g_return_val_if_fail (LRG_IS_POST_STACK (self), 0);
	return self->entries->len;
}

gint
lrg_post_stack_add_effect (LrgPostStack  *self,
                           const gchar   *type_name,
                           GError       **error)
{
	GType gtype;

	g_return_val_if_fail (LRG_IS_POST_STACK (self), -1);
	g_return_val_if_fail (type_name != NULL, -1);

	gtype = lrg_post_stack_lookup_effect_type (type_name);
	if (gtype == G_TYPE_INVALID)
	{
		g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_UNKNOWN_EFFECT,
		             "unknown effect type '%s'", type_name);
		return -1;
	}
	g_ptr_array_add (self->entries, stack_entry_new (type_name, gtype));
	return (gint) self->entries->len - 1;
}

void
lrg_post_stack_remove_effect (LrgPostStack *self,
                              guint         index)
{
	g_return_if_fail (LRG_IS_POST_STACK (self));
	g_return_if_fail (index < self->entries->len);

	g_ptr_array_remove_index (self->entries, index);
}

void
lrg_post_stack_move_effect (LrgPostStack *self,
                            guint         from,
                            guint         to)
{
	gpointer e;

	g_return_if_fail (LRG_IS_POST_STACK (self));
	g_return_if_fail (from < self->entries->len && to < self->entries->len);

	if (from == to)
		return;
	e = g_ptr_array_steal_index (self->entries, from);
	g_ptr_array_insert (self->entries, (gint) to, e);
}

gint
lrg_post_stack_find_effect (LrgPostStack *self,
                            const gchar  *type_name)
{
	GType gtype;
	guint i;

	g_return_val_if_fail (LRG_IS_POST_STACK (self), -1);
	g_return_val_if_fail (type_name != NULL, -1);

	gtype = lrg_post_stack_lookup_effect_type (type_name);
	for (i = 0; i < self->entries->len; i++)
	{
		StackEntry *e = g_ptr_array_index (self->entries, i);

		if (g_strcmp0 (e->type_name, type_name) == 0 || (gtype != G_TYPE_INVALID && e->gtype == gtype))
			return (gint) i;
	}
	return -1;
}

#define ENTRY_OR_RETURN(self, index, val) \
	g_return_val_if_fail (LRG_IS_POST_STACK (self), val); \
	g_return_val_if_fail ((index) < (self)->entries->len, val)

const gchar *
lrg_post_stack_get_effect_type (LrgPostStack *self,
                                guint         index)
{
	ENTRY_OR_RETURN (self, index, NULL);
	return ((StackEntry *) g_ptr_array_index (self->entries, index))->type_name;
}

gboolean
lrg_post_stack_get_effect_enabled (LrgPostStack *self,
                                   guint         index)
{
	ENTRY_OR_RETURN (self, index, FALSE);
	return ((StackEntry *) g_ptr_array_index (self->entries, index))->enabled;
}

void
lrg_post_stack_set_effect_enabled (LrgPostStack *self,
                                   guint         index,
                                   gboolean      enabled)
{
	g_return_if_fail (LRG_IS_POST_STACK (self));
	g_return_if_fail (index < self->entries->len);
	((StackEntry *) g_ptr_array_index (self->entries, index))->enabled = !!enabled;
}

gfloat
lrg_post_stack_get_effect_weight (LrgPostStack *self,
                                  guint         index)
{
	ENTRY_OR_RETURN (self, index, 0.0f);
	return ((StackEntry *) g_ptr_array_index (self->entries, index))->weight;
}

void
lrg_post_stack_set_effect_weight (LrgPostStack *self,
                                  guint         index,
                                  gfloat        weight)
{
	g_return_if_fail (LRG_IS_POST_STACK (self));
	g_return_if_fail (index < self->entries->len);
	if (!isfinite (weight))
		return;
	((StackEntry *) g_ptr_array_index (self->entries, index))->weight = CLAMP (weight, 0.0f, 1.0f);
}

gboolean
lrg_post_stack_set_param (LrgPostStack  *self,
                          guint          index,
                          const gchar   *name,
                          const GValue  *value,
                          GError       **error)
{
	StackEntry       *e;
	g_autofree gchar *canon = NULL;
	g_autofree gchar *what = NULL;
	g_auto(GValue)    v = G_VALUE_INIT;
	GParamSpec       *pspec = NULL;
	GType             type = G_TYPE_INVALID;

	ENTRY_OR_RETURN (self, index, FALSE);
	g_return_val_if_fail (name != NULL && G_IS_VALUE (value), FALSE);

	e = g_ptr_array_index (self->entries, index);
	canon = canonical_name (name);
	what = g_strdup_printf ("%s.%s", e->type_name, canon);
	if (!resolve_param (e, canon, &type, &pspec, error))
		return FALSE;
	if (!convert_value (value, type, pspec, what, &v, error))
		return FALSE;
	entry_set_param (e, canon, &v);
	return TRUE;
}

gboolean
lrg_post_stack_get_param (LrgPostStack *self,
                          guint         index,
                          const gchar  *name,
                          GValue       *value)
{
	g_autofree gchar *canon = NULL;
	StackParam       *p;

	ENTRY_OR_RETURN (self, index, FALSE);
	g_return_val_if_fail (name != NULL && value != NULL, FALSE);

	canon = canonical_name (name);
	p = entry_find_param (g_ptr_array_index (self->entries, index), canon);
	if (p == NULL)
		return FALSE;
	g_value_init (value, G_VALUE_TYPE (&p->value));
	g_value_copy (&p->value, value);
	return TRUE;
}

void
lrg_post_stack_unset_param (LrgPostStack *self,
                            guint         index,
                            const gchar  *name)
{
	g_autofree gchar *canon = NULL;
	StackEntry       *e;
	StackParam       *p;

	g_return_if_fail (LRG_IS_POST_STACK (self));
	g_return_if_fail (index < self->entries->len && name != NULL);

	canon = canonical_name (name);
	e = g_ptr_array_index (self->entries, index);
	p = entry_find_param (e, canon);
	if (p != NULL)
		g_ptr_array_remove (e->params, p);
}

gchar **
lrg_post_stack_get_param_names (LrgPostStack *self,
                                guint         index)
{
	g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
	StackEntry             *e;
	guint                   i;

	ENTRY_OR_RETURN (self, index, NULL);

	e = g_ptr_array_index (self->entries, index);
	for (i = 0; i < e->params->len; i++)
		g_strv_builder_add (b, ((StackParam *) g_ptr_array_index (e->params, i))->name);
	return g_strv_builder_end (b);
}

/* ==========================================================================
 * Build
 * ========================================================================== */

typedef struct
{
	StackEntry    *entry;
	LrgPostEffect *effect;   /* reused (borrowed) or new (owned in `fresh`) */
	gboolean       fresh;
	LrgColorLut   *lut;      /* preloaded, nullable */
} BuildSlot;

/* Remembers what special data was last applied to an effect, so a stack
 * built every frame does not re-upload palettes or reload LUTs */
#define APPLIED_PALETTE_KEY "lrg-post-stack-palette"
#define APPLIED_LUT_KEY     "lrg-post-stack-lut"

static gchar *
lut_path (LrgPostStack *self,
          const gchar  *path)
{
	if (path == NULL || g_path_is_absolute (path) || self->base_dir == NULL)
		return g_strdup (path);
	return g_build_filename (self->base_dir, path, NULL);
}

static gboolean
apply_palette (LrgPalettize  *effect,
               StackParam    *p,
               GError       **error)
{
	const gchar * const *colours = p != NULL ? g_value_get_boxed (&p->value) : NULL;
	g_autofree gchar    *joined = NULL;
	const gchar         *last = g_object_get_data (G_OBJECT (effect), APPLIED_PALETTE_KEY);
	g_autoptr(GByteArray) rgb = NULL;
	g_autoptr(GBytes)     bytes = NULL;
	gint                  i;

	joined = colours != NULL ? g_strjoinv (",", (gchar **) colours) : g_strdup ("");
	if (g_strcmp0 (last, joined) == 0)
		return TRUE;
	if (colours == NULL || colours[0] == NULL)
	{
		lrg_palettize_clear_palette (effect);
	}
	else
	{
		rgb = g_byte_array_new ();
		for (i = 0; colours[i] != NULL; i++)
		{
			guint8 c[3];

			parse_hex_rgb (colours[i], c);
			g_byte_array_append (rgb, c, 3);
		}
		bytes = g_byte_array_free_to_bytes (g_steal_pointer (&rgb));
		if (!lrg_palettize_set_palette (effect, bytes, error))
			return FALSE;
	}
	g_object_set_data_full (G_OBJECT (effect), APPLIED_PALETTE_KEY, g_steal_pointer (&joined), g_free);
	return TRUE;
}

/* Runtime state, not part of a look: a build leaves it alone unless the
 * stack sets it (a game adding trauma between builds keeps its shake) */
static gboolean
is_runtime_state (GType        effect_type,
                  const gchar *name)
{
	return g_type_is_a (effect_type, LRG_TYPE_SCREEN_SHAKE) && g_strcmp0 (name, "trauma") == 0;
}

/* Every effect property to its stack value or default; base properties */
static void
apply_properties (BuildSlot *slot,
                  gint       priority)
{
	LrgPostEffect         *effect = slot->effect;
	g_autofree GParamSpec **pspecs = NULL;
	guint                  n = 0, i;

	g_object_freeze_notify (G_OBJECT (effect));
	pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (effect), &n);
	for (i = 0; i < n; i++)
	{
		StackParam    *p;
		g_auto(GValue) cur = G_VALUE_INIT;
		g_auto(GValue) want = G_VALUE_INIT;

		if (!is_param_pspec (pspecs[i]))
			continue;
		p = entry_find_param (slot->entry, pspecs[i]->name);
		if (p == NULL && is_runtime_state (G_OBJECT_TYPE (effect), pspecs[i]->name))
			continue;
		g_value_init (&want, G_PARAM_SPEC_VALUE_TYPE (pspecs[i]));
		if (p != NULL)
			g_value_copy (&p->value, &want);
		else
			g_param_value_set_default (pspecs[i], &want);
		g_value_init (&cur, G_PARAM_SPEC_VALUE_TYPE (pspecs[i]));
		g_object_get_property (G_OBJECT (effect), pspecs[i]->name, &cur);
		if (g_param_values_cmp (pspecs[i], &cur, &want) != 0)
			g_object_set_property (G_OBJECT (effect), pspecs[i]->name, &want);
	}
	lrg_post_effect_set_enabled (effect, slot->entry->enabled);
	lrg_post_effect_set_intensity (effect, slot->entry->weight);
	lrg_post_effect_set_priority (effect, priority);
	g_object_thaw_notify (G_OBJECT (effect));
}

gboolean
lrg_post_stack_build (LrgPostStack      *self,
                      LrgPostProcessor  *processor,
                      GError           **error)
{
	g_autoptr(GArray)  slots = NULL;
	g_autoptr(GList)   existing = NULL;
	GList             *l;
	gboolean           ok = TRUE;
	guint              i;

	g_return_val_if_fail (LRG_IS_POST_STACK (self), FALSE);
	g_return_val_if_fail (LRG_IS_POST_PROCESSOR (processor), FALSE);

	/* 1. Plan: reuse a processor effect of the same type (first unclaimed
	 *    one), else create; preload LUTs. Nothing changes yet. */
	existing = g_list_copy (lrg_post_processor_get_effects (processor));
	slots = g_array_sized_new (FALSE, TRUE, sizeof (BuildSlot), self->entries->len);
	for (i = 0; i < self->entries->len && ok; i++)
	{
		BuildSlot   slot;
		StackParam *lut;

		memset (&slot, 0, sizeof slot);
		slot.entry = g_ptr_array_index (self->entries, i);
		for (l = existing; l != NULL; l = l->next)
			if (G_OBJECT_TYPE (l->data) == slot.entry->gtype)
			{
				slot.effect = l->data;
				existing = g_list_delete_link (existing, l);
				break;
			}
		if (slot.effect == NULL)
		{
			slot.effect = g_object_new (slot.entry->gtype, NULL);
			slot.fresh = TRUE;
		}

		lut = entry_find_param (slot.entry, "lut");
		if (lut != NULL && g_type_is_a (slot.entry->gtype, LRG_TYPE_COLOR_GRADE))
		{
			g_autofree gchar *path = lut_path (self, g_value_get_string (&lut->value));
			const gchar      *last = g_object_get_data (G_OBJECT (slot.effect), APPLIED_LUT_KEY);

			if (path != NULL && *path != '\0' && g_strcmp0 (last, path) != 0)
			{
				g_autoptr(GError) local = NULL;

				slot.lut = lrg_color_lut_new_from_file (path, &local);
				if (slot.lut == NULL)
				{
					g_set_error (error, LRG_POST_STACK_ERROR, LRG_POST_STACK_ERROR_IO,
					             "color-grade lut '%s': %s", path, local->message);
					ok = FALSE;
				}
			}
		}
		g_array_append_val (slots, slot);
	}
	if (ok)
	{
		/* palettes are validated colours; set_palette only fails on size */
		for (i = 0; i < slots->len && ok; i++)
		{
			BuildSlot *s = &g_array_index (slots, BuildSlot, i);

			if (s->fresh && LRG_IS_PALETTIZE (s->effect) && !apply_palette (LRG_PALETTIZE (s->effect),
			                                                                 entry_find_param (s->entry, "palette"),
			                                                                 error))
				ok = FALSE;
		}
	}
	if (!ok)
	{
		for (i = 0; i < slots->len; i++)
		{
			BuildSlot *s = &g_array_index (slots, BuildSlot, i);

			if (s->fresh)
				g_object_unref (s->effect);
			g_clear_pointer (&s->lut, lrg_color_lut_unref);
		}
		return FALSE;
	}

	/* 2. Drop what the stack no longer has */
	for (l = existing; l != NULL; l = l->next)
		lrg_post_processor_remove_effect (processor, LRG_POST_EFFECT (l->data));

	/* 3. Settings, then add the new effects (initialised and given the
	 *    quality by the processor) */
	lrg_post_processor_set_hdr (processor, self->hdr);
	lrg_post_processor_set_quality (processor, self->quality);
	for (i = 0; i < slots->len; i++)
	{
		BuildSlot  *s = &g_array_index (slots, BuildSlot, i);
		StackParam *strength;

		apply_properties (s, s->entry->has_priority ? s->entry->priority : (gint) i * 10);

		if (LRG_IS_PALETTIZE (s->effect) && !s->fresh)
			apply_palette (LRG_PALETTIZE (s->effect), entry_find_param (s->entry, "palette"), NULL);
		if (LRG_IS_COLOR_GRADE (s->effect))
		{
			StackParam *lut = entry_find_param (s->entry, "lut");
			gfloat      k;

			strength = entry_find_param (s->entry, "lut-strength");
			k = strength != NULL ? g_value_get_float (&strength->value) : 1.0f;
			if (s->lut != NULL)
			{
				lrg_color_grade_set_lut (LRG_COLOR_GRADE (s->effect), s->lut, k);
				g_object_set_data_full (G_OBJECT (s->effect), APPLIED_LUT_KEY,
				                        lut_path (self, g_value_get_string (&lut->value)), g_free);
			}
			else if (lut == NULL && g_object_get_data (G_OBJECT (s->effect), APPLIED_LUT_KEY) != NULL)
			{
				lrg_color_grade_set_lut (LRG_COLOR_GRADE (s->effect), NULL, 0.0f);
				g_object_set_data (G_OBJECT (s->effect), APPLIED_LUT_KEY, NULL);
			}
			else if (lut != NULL)
				lrg_color_grade_set_lut (LRG_COLOR_GRADE (s->effect),
				                         lrg_color_grade_get_lut (LRG_COLOR_GRADE (s->effect), NULL), k);
		}
		if (s->fresh)
		{
			lrg_post_processor_add_effect (processor, s->effect);
			g_object_unref (s->effect);
		}
		g_clear_pointer (&s->lut, lrg_color_lut_unref);
	}
	lrg_post_processor_sort_effects (processor);
	return TRUE;
}

/* ==========================================================================
 * Blend
 * ========================================================================== */

/* The value an entry gives @name: its own, else the property default */
static gboolean
entry_value (StackEntry  *e,
             const gchar *name,
             GValue      *out)
{
	StackParam *p = entry_find_param (e, name);
	GParamSpec *pspec;

	if (p != NULL)
	{
		g_value_init (out, G_VALUE_TYPE (&p->value));
		g_value_copy (&p->value, out);
		return TRUE;
	}
	pspec = find_param_pspec (e->gtype, name);
	if (pspec == NULL)
	{
		/* special params default to "none" */
		GType t = special_param_type (e->gtype, name);

		if (t == G_TYPE_INVALID)
			return FALSE;
		g_value_init (out, t);
		if (t == G_TYPE_FLOAT)
			g_value_set_float (out, 1.0f);
		return TRUE;
	}
	g_value_init (out, G_PARAM_SPEC_VALUE_TYPE (pspec));
	g_param_value_set_default (pspec, out);
	return TRUE;
}

static void
lerp_value (const GValue *a,
            const GValue *b,
            gfloat        t,
            GValue       *out)
{
	GType fund = G_TYPE_FUNDAMENTAL (G_VALUE_TYPE (a));

	g_value_init (out, G_VALUE_TYPE (a));
	switch (fund)
	{
	case G_TYPE_FLOAT:
		g_value_set_float (out, g_value_get_float (a) + (g_value_get_float (b) - g_value_get_float (a)) * t);
		break;
	case G_TYPE_DOUBLE:
		g_value_set_double (out, g_value_get_double (a) + (g_value_get_double (b) - g_value_get_double (a)) * t);
		break;
	case G_TYPE_INT:
		g_value_set_int (out, (gint) lround (g_value_get_int (a) + (gdouble) (g_value_get_int (b) - g_value_get_int (a)) * t));
		break;
	case G_TYPE_UINT:
		g_value_set_uint (out, (guint) lround ((gdouble) g_value_get_uint (a) +
		                                       ((gdouble) g_value_get_uint (b) - (gdouble) g_value_get_uint (a)) * t));
		break;
	default:
		g_value_copy (t < 0.5f ? a : b, out);
		break;
	}
}

static void
blend_entries (StackEntry *out,
               StackEntry *a,
               StackEntry *b,
               gfloat      t)
{
	g_autoptr(GPtrArray) names = g_ptr_array_new ();
	gfloat               wa = a->enabled ? a->weight : 0.0f;
	gfloat               wb = b->enabled ? b->weight : 0.0f;
	guint                i;

	/* union of the names, a's order first */
	for (i = 0; i < a->params->len; i++)
		g_ptr_array_add (names, ((StackParam *) g_ptr_array_index (a->params, i))->name);
	for (i = 0; i < b->params->len; i++)
	{
		const gchar *n = ((StackParam *) g_ptr_array_index (b->params, i))->name;

		if (entry_find_param (a, n) == NULL)
			g_ptr_array_add (names, (gpointer) n);
	}
	for (i = 0; i < names->len; i++)
	{
		const gchar   *n = g_ptr_array_index (names, i);
		g_auto(GValue) va = G_VALUE_INIT;
		g_auto(GValue) vb = G_VALUE_INIT;
		g_auto(GValue) vr = G_VALUE_INIT;

		if (!entry_value (a, n, &va) || !entry_value (b, n, &vb) ||
		    G_VALUE_TYPE (&va) != G_VALUE_TYPE (&vb))
			continue;
		lerp_value (&va, &vb, t, &vr);
		entry_set_param (out, n, &vr);
	}
	out->weight = wa + (wb - wa) * t;
	out->enabled = out->weight > 0.0f;
	out->has_priority = a->has_priority;
	out->priority = a->priority;
}

LrgPostStack *
lrg_post_stack_blend (LrgPostStack *a,
                      LrgPostStack *b,
                      gfloat        weight)
{
	LrgPostStack     *out;
	g_autofree gboolean *b_used = NULL;
	guint             i, j;

	g_return_val_if_fail (LRG_IS_POST_STACK (a), NULL);
	g_return_val_if_fail (LRG_IS_POST_STACK (b), NULL);

	weight = isfinite (weight) ? CLAMP (weight, 0.0f, 1.0f) : 0.0f;
	out = lrg_post_stack_new ();
	out->name = g_strdup (a->name);
	out->quality = weight < 0.5f ? a->quality : b->quality;
	out->hdr = a->hdr || b->hdr;
	out->base_dir = g_strdup (weight < 0.5f ? a->base_dir : b->base_dir);
	b_used = g_new0 (gboolean, MAX (b->entries->len, 1u));

	for (i = 0; i < a->entries->len; i++)
	{
		StackEntry *ea = g_ptr_array_index (a->entries, i);
		StackEntry *eb = NULL;
		StackEntry *er;

		/* the first unused effect of the same type in b */
		for (j = 0; j < b->entries->len; j++)
			if (!b_used[j] && ((StackEntry *) g_ptr_array_index (b->entries, j))->gtype == ea->gtype)
			{
				eb = g_ptr_array_index (b->entries, j);
				b_used[j] = TRUE;
				break;
			}
		if (eb != NULL)
		{
			er = stack_entry_new (ea->type_name, ea->gtype);
			blend_entries (er, ea, eb, weight);
		}
		else
		{
			er = stack_entry_copy (ea);
			er->weight = (ea->enabled ? ea->weight : 0.0f) * (1.0f - weight);
			er->enabled = er->weight > 0.0f;
		}
		g_ptr_array_add (out->entries, er);
	}
	for (j = 0; j < b->entries->len; j++)
	{
		StackEntry *eb = g_ptr_array_index (b->entries, j);
		StackEntry *er;

		if (b_used[j])
			continue;
		er = stack_entry_copy (eb);
		er->weight = (eb->enabled ? eb->weight : 0.0f) * weight;
		er->enabled = er->weight > 0.0f;
		g_ptr_array_add (out->entries, er);
	}
	return out;
}
