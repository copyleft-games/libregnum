/* lrg-retargeter.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Bone maps and retargeting.
 *
 * Rest-pose alignment. Let Sr(s) and Tr(t) be the skeleton-space (world)
 * rest rotations of a source bone s and the target bone t it drives, and
 * Ws(s) the source bone's animated world rotation. The world-space change
 * of the source bone is D = Ws(s) Sr(s)^-1; the target bone takes the same
 * change on top of its own rest: Wt(t) = D Tr(t). Its local rotation is
 * then Wp^-1 Wt(t) with Wp the target parent's animated world rotation,
 * so the target hierarchy is walked parents first. Unmapped target bones
 * keep their bind local pose (and inherit their parent's motion).
 *
 * Translation: a moving bone's world position is its rest world position
 * plus the source bone's world displacement from rest times the
 * translation scale (automatic: ratio of the root bones' rest heights),
 * brought back into the parent's frame.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <gio/gio.h>
#include <yaml-glib.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "lrg-retargeter.h"
#include "lrg-bone.h"
#include "lrg-bone-pose.h"

/* ------------------------------------------------------------------------ */
/* Error domain and enums                                                   */
/* ------------------------------------------------------------------------ */

G_DEFINE_QUARK (lrg-retarget-error-quark, lrg_retarget_error)

GType
lrg_retarget_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_RETARGET_ERROR_IO, "LRG_RETARGET_ERROR_IO", "io" },
			{ LRG_RETARGET_ERROR_PARSE, "LRG_RETARGET_ERROR_PARSE", "parse" },
			{ LRG_RETARGET_ERROR_VERSION, "LRG_RETARGET_ERROR_VERSION", "version" },
			{ LRG_RETARGET_ERROR_SKELETON, "LRG_RETARGET_ERROR_SKELETON", "skeleton" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgRetargetError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

GType
lrg_retarget_translation_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_RETARGET_TRANSLATION_NONE, "LRG_RETARGET_TRANSLATION_NONE", "none" },
			{ LRG_RETARGET_TRANSLATION_ROOT, "LRG_RETARGET_TRANSLATION_ROOT", "root" },
			{ LRG_RETARGET_TRANSLATION_ALL, "LRG_RETARGET_TRANSLATION_ALL", "all" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgRetargetTranslationMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

/* ======================================================================== */
/* LrgBoneMap                                                               */
/* ======================================================================== */

struct _LrgBoneMap
{
	GObject     parent_instance;

	GHashTable *pairs;          /* source -> target (owned strings) */
	GPtrArray  *prefixes;       /* gchar*, NULL-terminated */
	gboolean    ignore_case;
	gboolean    normalize_separators;
	gboolean    normalize_sides;
};

enum
{
	MAP_PROP_0,
	MAP_PROP_IGNORE_CASE,
	MAP_PROP_NORMALIZE_SEPARATORS,
	MAP_PROP_NORMALIZE_SIDES,
	MAP_N_PROPS
};

enum
{
	MAP_SIGNAL_CHANGED,
	MAP_N_SIGNALS
};

static GParamSpec *map_props[MAP_N_PROPS];
static guint       map_signals[MAP_N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgBoneMap, lrg_bone_map, G_TYPE_OBJECT)

static void
bone_map_changed (LrgBoneMap *self)
{
	g_signal_emit (self, map_signals[MAP_SIGNAL_CHANGED], 0);
}

static void
lrg_bone_map_finalize (GObject *object)
{
	LrgBoneMap *self = LRG_BONE_MAP (object);

	g_clear_pointer (&self->pairs, g_hash_table_unref);
	g_clear_pointer (&self->prefixes, g_ptr_array_unref);
	G_OBJECT_CLASS (lrg_bone_map_parent_class)->finalize (object);
}

static void
lrg_bone_map_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	LrgBoneMap *self = LRG_BONE_MAP (object);

	switch (prop_id)
	{
	case MAP_PROP_IGNORE_CASE:
		g_value_set_boolean (value, self->ignore_case);
		break;
	case MAP_PROP_NORMALIZE_SEPARATORS:
		g_value_set_boolean (value, self->normalize_separators);
		break;
	case MAP_PROP_NORMALIZE_SIDES:
		g_value_set_boolean (value, self->normalize_sides);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_bone_map_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	LrgBoneMap *self = LRG_BONE_MAP (object);

	switch (prop_id)
	{
	case MAP_PROP_IGNORE_CASE:
		lrg_bone_map_set_ignore_case (self, g_value_get_boolean (value));
		break;
	case MAP_PROP_NORMALIZE_SEPARATORS:
		lrg_bone_map_set_normalize_separators (self, g_value_get_boolean (value));
		break;
	case MAP_PROP_NORMALIZE_SIDES:
		lrg_bone_map_set_normalize_sides (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_bone_map_class_init (LrgBoneMapClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_bone_map_finalize;
	object_class->get_property = lrg_bone_map_get_property;
	object_class->set_property = lrg_bone_map_set_property;

	/**
	 * LrgBoneMap:ignore-case:
	 *
	 * Whether the name rules compare bone names without case.
	 */
	map_props[MAP_PROP_IGNORE_CASE] =
		g_param_spec_boolean ("ignore-case", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBoneMap:normalize-separators:
	 *
	 * Whether bone names are split into words (separators and case
	 * changes) before they are compared.
	 */
	map_props[MAP_PROP_NORMALIZE_SEPARATORS] =
		g_param_spec_boolean ("normalize-separators", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgBoneMap:normalize-sides:
	 *
	 * Whether "left"/"l" and "right"/"r" words are treated as a side
	 * marker wherever they appear.
	 */
	map_props[MAP_PROP_NORMALIZE_SIDES] =
		g_param_spec_boolean ("normalize-sides", NULL, NULL, TRUE,
		                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, MAP_N_PROPS, map_props);

	/**
	 * LrgBoneMap::changed:
	 * @self: the bone map
	 *
	 * Emitted after a pair or a name rule changed (the retargeter and the
	 * editor's bone map panel refresh on it).
	 */
	map_signals[MAP_SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_bone_map_init (LrgBoneMap *self)
{
	self->pairs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->prefixes = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (self->prefixes, NULL);
	self->ignore_case = TRUE;
	self->normalize_separators = TRUE;
	self->normalize_sides = TRUE;
}

LrgBoneMap *
lrg_bone_map_new (void)
{
	return g_object_new (LRG_TYPE_BONE_MAP, NULL);
}

/* ------------------------------------------------------------------------ */
/* Pairs and rules                                                          */
/* ------------------------------------------------------------------------ */

void
lrg_bone_map_set (LrgBoneMap  *self,
                  const gchar *source,
                  const gchar *target)
{
	GHashTableIter iter;
	gpointer       key;
	gpointer       value;

	g_return_if_fail (LRG_IS_BONE_MAP (self));
	g_return_if_fail (source != NULL);

	/* one source per target: drop an older pair naming @target */
	if (target != NULL)
	{
		g_hash_table_iter_init (&iter, self->pairs);
		while (g_hash_table_iter_next (&iter, &key, &value))
			if (g_strcmp0 (value, target) == 0 && g_strcmp0 (key, source) != 0)
				g_hash_table_iter_remove (&iter);
		g_hash_table_replace (self->pairs, g_strdup (source), g_strdup (target));
	}
	else
		g_hash_table_remove (self->pairs, source);
	bone_map_changed (self);
}

const gchar *
lrg_bone_map_get_target (LrgBoneMap  *self,
                         const gchar *source)
{
	g_return_val_if_fail (LRG_IS_BONE_MAP (self), NULL);
	g_return_val_if_fail (source != NULL, NULL);
	return g_hash_table_lookup (self->pairs, source);
}

const gchar *
lrg_bone_map_get_source (LrgBoneMap  *self,
                         const gchar *target)
{
	GHashTableIter iter;
	gpointer       key;
	gpointer       value;

	g_return_val_if_fail (LRG_IS_BONE_MAP (self), NULL);
	g_return_val_if_fail (target != NULL, NULL);

	g_hash_table_iter_init (&iter, self->pairs);
	while (g_hash_table_iter_next (&iter, &key, &value))
		if (g_strcmp0 (value, target) == 0)
			return key;
	return NULL;
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *)a, *(const gchar * const *)b);
}

gchar **
lrg_bone_map_get_sources (LrgBoneMap *self)
{
	GPtrArray     *out;
	GHashTableIter iter;
	gpointer       key;

	g_return_val_if_fail (LRG_IS_BONE_MAP (self), NULL);

	out = g_ptr_array_new ();
	g_hash_table_iter_init (&iter, self->pairs);
	while (g_hash_table_iter_next (&iter, &key, NULL))
		g_ptr_array_add (out, g_strdup (key));
	g_ptr_array_sort (out, compare_strings);
	g_ptr_array_add (out, NULL);
	return (gchar **)g_ptr_array_free (out, FALSE);
}

guint
lrg_bone_map_get_size (LrgBoneMap *self)
{
	g_return_val_if_fail (LRG_IS_BONE_MAP (self), 0);
	return g_hash_table_size (self->pairs);
}

void
lrg_bone_map_clear (LrgBoneMap *self)
{
	g_return_if_fail (LRG_IS_BONE_MAP (self));
	g_hash_table_remove_all (self->pairs);
	bone_map_changed (self);
}

void
lrg_bone_map_add_strip_prefix (LrgBoneMap  *self,
                               const gchar *prefix)
{
	g_return_if_fail (LRG_IS_BONE_MAP (self));
	g_return_if_fail (prefix != NULL && prefix[0] != '\0');

	/* keep the array NULL-terminated */
	g_ptr_array_index (self->prefixes, self->prefixes->len - 1) = g_strdup (prefix);
	g_ptr_array_add (self->prefixes, NULL);
	bone_map_changed (self);
}

const gchar * const *
lrg_bone_map_get_strip_prefixes (LrgBoneMap *self)
{
	g_return_val_if_fail (LRG_IS_BONE_MAP (self), NULL);
	return (const gchar * const *)self->prefixes->pdata;
}

#define BONE_MAP_FLAG(name, field, prop)                                       \
void                                                                           \
lrg_bone_map_set_##name (LrgBoneMap *self,                                     \
                         gboolean    value)                                    \
{                                                                              \
	g_return_if_fail (LRG_IS_BONE_MAP (self));                                 \
	value = value != FALSE;                                                    \
	if (self->field == value)                                                  \
		return;                                                                \
	self->field = value;                                                       \
	g_object_notify_by_pspec (G_OBJECT (self), map_props[prop]);              \
	bone_map_changed (self);                                                   \
}                                                                              \
                                                                               \
gboolean                                                                       \
lrg_bone_map_get_##name (LrgBoneMap *self)                                     \
{                                                                              \
	g_return_val_if_fail (LRG_IS_BONE_MAP (self), FALSE);                      \
	return self->field;                                                        \
}

BONE_MAP_FLAG (ignore_case, ignore_case, MAP_PROP_IGNORE_CASE)
BONE_MAP_FLAG (normalize_separators, normalize_separators, MAP_PROP_NORMALIZE_SEPARATORS)
BONE_MAP_FLAG (normalize_sides, normalize_sides, MAP_PROP_NORMALIZE_SIDES)

/* does @name start with @prefix (case-insensitively when asked)? */
static gboolean
has_prefix (const gchar *name,
            const gchar *prefix,
            gboolean     ignore_case)
{
	gsize n = strlen (prefix);

	if (strlen (name) < n)
		return FALSE;
	return ignore_case ? g_ascii_strncasecmp (name, prefix, n) == 0 : strncmp (name, prefix, n) == 0;
}

gchar *
lrg_bone_map_normalize_name (LrgBoneMap  *self,
                             const gchar *name)
{
	const gchar       *rest;
	gboolean           stripped;
	g_autoptr(GString) word = NULL;
	g_autoptr(GPtrArray) words = NULL;
	GString           *out;
	const gchar       *side = NULL;
	const gchar       *p;
	guint              i;

	g_return_val_if_fail (LRG_IS_BONE_MAP (self), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	/* 1. strip prefixes (repeatedly: "Armature|mixamorig:Hips") */
	rest = name;
	do
	{
		stripped = FALSE;
		for (i = 0; i + 1 < self->prefixes->len; i++)
		{
			const gchar *prefix = g_ptr_array_index (self->prefixes, i);

			if (has_prefix (rest, prefix, self->ignore_case))
			{
				rest += strlen (prefix);
				stripped = TRUE;
			}
		}
	} while (stripped && rest[0] != '\0');

	if (!self->normalize_separators)
		return self->ignore_case ? g_ascii_strdown (rest, -1) : g_strdup (rest);

	/* 2. split into words at separators and lower->upper case changes */
	words = g_ptr_array_new_with_free_func (g_free);
	word = g_string_new (NULL);
	for (p = rest; *p != '\0'; p++)
	{
		gboolean sep = strchr ("_-.:| ", *p) != NULL;
		/* compare against the original previous character: the word
		 * itself may already be lower-cased */
		gboolean camel = word->len > 0 && p > rest && g_ascii_isupper (*p) &&
		                 g_ascii_islower (p[-1]);

		if ((sep || camel) && word->len > 0)
		{
			g_ptr_array_add (words, g_string_free (g_steal_pointer (&word), FALSE));
			word = g_string_new (NULL);
		}
		if (!sep)
			g_string_append_c (word, self->ignore_case ? g_ascii_tolower (*p) : *p);
	}
	if (word->len > 0)
		g_ptr_array_add (words, g_string_free (g_steal_pointer (&word), FALSE));

	/* 3. side words become a trailing marker */
	out = g_string_new (NULL);
	for (i = 0; i < words->len; i++)
	{
		const gchar *w = g_ptr_array_index (words, i);

		if (self->normalize_sides && side == NULL)
		{
			if (g_ascii_strcasecmp (w, "left") == 0 || g_ascii_strcasecmp (w, "l") == 0)
			{
				side = "l";
				continue;
			}
			if (g_ascii_strcasecmp (w, "right") == 0 || g_ascii_strcasecmp (w, "r") == 0)
			{
				side = "r";
				continue;
			}
		}
		if (out->len > 0)
			g_string_append_c (out, '_');
		g_string_append (out, w);
	}
	if (side != NULL)
		g_string_append_printf (out, ".%s", side);
	return g_string_free (out, FALSE);
}

gint *
lrg_bone_map_resolve (LrgBoneMap  *self,
                      LrgSkeleton *source,
                      LrgSkeleton *target,
                      guint       *n_target)
{
	g_autoptr(GHashTable) canon = NULL;   /* canonical source name -> index + 1 */
	g_autofree guint8    *used = NULL;
	guint                 ns;
	guint                 nt;
	gint                 *out;
	guint                 i;

	g_return_val_if_fail (LRG_IS_BONE_MAP (self), NULL);
	g_return_val_if_fail (LRG_IS_SKELETON (source), NULL);
	g_return_val_if_fail (LRG_IS_SKELETON (target), NULL);

	ns = lrg_skeleton_get_bone_count (source);
	nt = lrg_skeleton_get_bone_count (target);
	if (n_target != NULL)
		*n_target = nt;
	out = g_new (gint, MAX (nt, 1));
	used = g_new0 (guint8, MAX (ns, 1));

	/* explicit pairs first */
	for (i = 0; i < nt; i++)
	{
		LrgBone     *tb = lrg_skeleton_get_bone (target, (gint)i);
		const gchar *src = tb != NULL ? lrg_bone_map_get_source (self, lrg_bone_get_name (tb)) : NULL;
		LrgBone     *sb = src != NULL ? lrg_skeleton_get_bone_by_name (source, src) : NULL;

		out[i] = -1;
		if (sb != NULL && lrg_bone_get_index (sb) >= 0 && (guint)lrg_bone_get_index (sb) < ns)
		{
			out[i] = lrg_bone_get_index (sb);
			used[out[i]] = 1;
		}
	}

	/* then the name rules, first come first served */
	canon = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (i = 0; i < ns; i++)
	{
		LrgBone *sb = lrg_skeleton_get_bone (source, (gint)i);

		if (sb == NULL || used[i] || lrg_bone_get_name (sb) == NULL)
			continue;
		/* explicitly mapped sources do not take part in the rules */
		if (lrg_bone_map_get_target (self, lrg_bone_get_name (sb)) != NULL)
			continue;
		{
			gchar *key = lrg_bone_map_normalize_name (self, lrg_bone_get_name (sb));

			if (!g_hash_table_contains (canon, key))
				g_hash_table_insert (canon, key, GINT_TO_POINTER ((gint)i + 1));
			else
				g_free (key);
		}
	}
	for (i = 0; i < nt; i++)
	{
		LrgBone          *tb = lrg_skeleton_get_bone (target, (gint)i);
		g_autofree gchar *key = NULL;
		gint              s;

		if (out[i] >= 0 || tb == NULL || lrg_bone_get_name (tb) == NULL)
			continue;
		key = lrg_bone_map_normalize_name (self, lrg_bone_get_name (tb));
		s = GPOINTER_TO_INT (g_hash_table_lookup (canon, key)) - 1;
		if (s >= 0 && !used[s])
		{
			out[i] = s;
			used[s] = 1;
		}
	}
	return out;
}

guint
lrg_bone_map_auto_fill (LrgBoneMap  *self,
                        LrgSkeleton *source,
                        LrgSkeleton *target)
{
	g_autofree gint *pairs = NULL;
	guint            nt = 0;
	guint            added = 0;
	guint            i;

	g_return_val_if_fail (LRG_IS_BONE_MAP (self), 0);

	pairs = lrg_bone_map_resolve (self, source, target, &nt);
	for (i = 0; i < nt; i++)
	{
		LrgBone *tb = lrg_skeleton_get_bone (target, (gint)i);
		LrgBone *sb;

		if (pairs[i] < 0 || tb == NULL)
			continue;
		sb = lrg_skeleton_get_bone (source, pairs[i]);
		if (sb == NULL || lrg_bone_map_get_target (self, lrg_bone_get_name (sb)) != NULL)
			continue;
		g_hash_table_replace (self->pairs, g_strdup (lrg_bone_get_name (sb)),
		                      g_strdup (lrg_bone_get_name (tb)));
		added++;
	}
	if (added > 0)
		bone_map_changed (self);
	return added;
}

/* ------------------------------------------------------------------------ */
/* .rbonemap                                                                */
/* ------------------------------------------------------------------------ */

static const gchar *
scalar_of (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

/* a YAML boolean scalar; FALSE with an error otherwise */
static gboolean
read_bool (YamlNode     *node,
           const gchar  *what,
           gboolean     *out,
           GError      **error)
{
	const gchar *s = scalar_of (node);

	if (s != NULL && (g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "yes") == 0))
		*out = TRUE;
	else if (s != NULL && (g_ascii_strcasecmp (s, "false") == 0 || g_ascii_strcasecmp (s, "no") == 0))
		*out = FALSE;
	else
	{
		g_set_error (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_PARSE,
		             "rbonemap: '%s' must be true or false", what);
		return FALSE;
	}
	return TRUE;
}

LrgBoneMap *
lrg_bone_map_new_from_data (const gchar  *data,
                            gssize        length,
                            GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autoptr(GError)     local = NULL;
	g_autoptr(LrgBoneMap) self = NULL;
	YamlNode             *root;
	YamlMapping          *map;
	YamlNode             *node;
	const gchar          *version;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, &local))
	{
		g_set_error (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_PARSE, "rbonemap: %s", local->message);
		return NULL;
	}
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_PARSE,
		                     "rbonemap: the document must be a mapping");
		return NULL;
	}
	map = yaml_node_get_mapping (root);

	/* version: absent means 1 */
	version = scalar_of (yaml_mapping_get_member (map, "version"));
	if (version != NULL && g_ascii_strtoll (version, NULL, 10) > LRG_BONE_MAP_FORMAT_VERSION)
	{
		g_set_error (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_VERSION,
		             "rbonemap: version %s is newer than %d", version, LRG_BONE_MAP_FORMAT_VERSION);
		return NULL;
	}

	self = lrg_bone_map_new ();

	/* rules */
	node = yaml_mapping_get_member (map, "rules");
	if (node != NULL && !yaml_node_is_null (node))
	{
		YamlMapping *rules;
		YamlNode    *prefixes;
		gboolean     flag;

		if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		{
			g_set_error_literal (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_PARSE,
			                     "rbonemap: 'rules' must be a mapping");
			return NULL;
		}
		rules = yaml_node_get_mapping (node);
		if (yaml_mapping_has_member (rules, "ignore_case"))
		{
			if (!read_bool (yaml_mapping_get_member (rules, "ignore_case"), "ignore_case", &flag, error))
				return NULL;
			self->ignore_case = flag;
		}
		if (yaml_mapping_has_member (rules, "normalize_separators"))
		{
			if (!read_bool (yaml_mapping_get_member (rules, "normalize_separators"),
			                "normalize_separators", &flag, error))
				return NULL;
			self->normalize_separators = flag;
		}
		if (yaml_mapping_has_member (rules, "normalize_sides"))
		{
			if (!read_bool (yaml_mapping_get_member (rules, "normalize_sides"), "normalize_sides", &flag, error))
				return NULL;
			self->normalize_sides = flag;
		}
		prefixes = yaml_mapping_get_member (rules, "strip_prefixes");
		if (prefixes != NULL && !yaml_node_is_null (prefixes))
		{
			YamlSequence *seq;
			guint         i;

			if (yaml_node_get_node_type (prefixes) != YAML_NODE_SEQUENCE)
			{
				g_set_error_literal (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_PARSE,
				                     "rbonemap: 'strip_prefixes' must be a list");
				return NULL;
			}
			seq = yaml_node_get_sequence (prefixes);
			for (i = 0; i < yaml_sequence_get_length (seq); i++)
			{
				const gchar *s = scalar_of (yaml_sequence_get_element (seq, i));

				if (s == NULL || s[0] == '\0')
				{
					g_set_error (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_PARSE,
					             "rbonemap: strip prefix %u is not a string", i);
					return NULL;
				}
				lrg_bone_map_add_strip_prefix (self, s);
			}
		}
	}

	/* bones: source: target */
	node = yaml_mapping_get_member (map, "bones");
	if (node != NULL && !yaml_node_is_null (node))
	{
		YamlMapping *bones;
		guint        i;

		if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		{
			g_set_error_literal (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_PARSE,
			                     "rbonemap: 'bones' must be a mapping of source: target");
			return NULL;
		}
		bones = yaml_node_get_mapping (node);
		for (i = 0; i < yaml_mapping_get_size (bones); i++)
		{
			const gchar *source = yaml_mapping_get_key (bones, i);
			const gchar *target = scalar_of (yaml_mapping_get_value (bones, i));

			if (source == NULL || target == NULL || target[0] == '\0')
			{
				g_set_error (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_PARSE,
				             "rbonemap: bone '%s' needs a target name", source != NULL ? source : "?");
				return NULL;
			}
			lrg_bone_map_set (self, source, target);
		}
	}
	return g_steal_pointer (&self);
}

LrgBoneMap *
lrg_bone_map_new_from_file (const gchar  *path,
                            GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;
	LrgBoneMap       *self;
	gsize             len = 0;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_file_get_contents (path, &text, &len, &local))
	{
		g_set_error (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_IO, "%s", local->message);
		return NULL;
	}
	self = lrg_bone_map_new_from_data (text, (gssize)len, &local);
	if (self == NULL)
	{
		g_set_error (error, LRG_RETARGET_ERROR, local->code, "%s: %s", path, local->message);
		return NULL;
	}
	return self;
}

gchar *
lrg_bone_map_to_data (LrgBoneMap  *self,
                      GError     **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlNode)      rules = NULL;
	g_autoptr(YamlNode)      bones = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	g_auto(GStrv)            sources = NULL;
	YamlMapping             *map;
	YamlMapping             *rmap;
	YamlMapping             *bmap;
	guint                    i;

	g_return_val_if_fail (LRG_IS_BONE_MAP (self), NULL);

	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_int_member (map, "version", LRG_BONE_MAP_FORMAT_VERSION);

	rules = yaml_node_new_mapping (NULL);
	rmap = yaml_node_get_mapping (rules);
	yaml_mapping_set_boolean_member (rmap, "ignore_case", self->ignore_case);
	yaml_mapping_set_boolean_member (rmap, "normalize_separators", self->normalize_separators);
	yaml_mapping_set_boolean_member (rmap, "normalize_sides", self->normalize_sides);
	if (self->prefixes->len > 1)
	{
		g_autoptr(YamlNode) list = yaml_node_new_sequence (NULL);
		YamlSequence       *seq = yaml_node_get_sequence (list);

		for (i = 0; i + 1 < self->prefixes->len; i++)
			yaml_sequence_add_string_element (seq, g_ptr_array_index (self->prefixes, i));
		yaml_mapping_set_member (rmap, "strip_prefixes", list);
	}
	yaml_mapping_set_member (map, "rules", rules);

	bones = yaml_node_new_mapping (NULL);
	bmap = yaml_node_get_mapping (bones);
	sources = lrg_bone_map_get_sources (self);
	for (i = 0; sources[i] != NULL; i++)
		yaml_mapping_set_string_member (bmap, sources[i], g_hash_table_lookup (self->pairs, sources[i]));
	yaml_mapping_set_member (map, "bones", bones);

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, NULL, error);
}

gboolean
lrg_bone_map_save_to_file (LrgBoneMap   *self,
                           const gchar  *path,
                           GError      **error)
{
	g_autofree gchar *text = NULL;
	g_autoptr(GError) local = NULL;

	g_return_val_if_fail (LRG_IS_BONE_MAP (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	text = lrg_bone_map_to_data (self, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, -1, &local))
	{
		g_set_error (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return TRUE;
}

/* ======================================================================== */
/* Quaternion helpers                                                       */
/* ======================================================================== */

static void
q_mul (const gfloat *a,
       const gfloat *b,
       gfloat       *q)
{
	gfloat out[4];

	out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	memcpy (q, out, sizeof out);
}

static void
q_conj (const gfloat *a,
        gfloat       *q)
{
	q[0] = -a[0];
	q[1] = -a[1];
	q[2] = -a[2];
	q[3] = a[3];
}

static void
q_norm (gfloat *q)
{
	gfloat len = sqrtf (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);

	if (len > 1e-12f)
	{
		q[0] /= len;
		q[1] /= len;
		q[2] /= len;
		q[3] /= len;
	}
	else
	{
		q[0] = q[1] = q[2] = 0.0f;
		q[3] = 1.0f;
	}
}

static void
q_rotate (const gfloat *q,
          const gfloat *v,
          gfloat       *out)
{
	gfloat tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
	gfloat ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
	gfloat tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
	gfloat r[3];

	r[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
	r[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
	r[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
	memcpy (out, r, sizeof r);
}

static void
pose_rot (const LrgBonePose *p,
          gfloat            *q)
{
	q[0] = p->rotation_x;
	q[1] = p->rotation_y;
	q[2] = p->rotation_z;
	q[3] = p->rotation_w;
	q_norm (q);
}

/* ======================================================================== */
/* LrgRetargeter                                                            */
/* ======================================================================== */

struct _LrgRetargeter
{
	GObject                     parent_instance;

	LrgSkeleton                *source;
	LrgSkeleton                *target;
	LrgBoneMap                 *map;
	gulong                      map_handler;

	LrgRetargetTranslationMode  translation_mode;
	gfloat                      translation_scale;   /* 0: automatic */
	gchar                      *root_bone;           /* nullable: topmost mapped */
	gfloat                      sample_rate;

	/* resolved state (valid when !dirty) */
	gboolean                    dirty;
	gboolean                    ok;
	guint                       ns;
	guint                       nt;
	gint                       *mapping;      /* per target: source or -1 */
	gint                       *src_parent;   /* per source bone */
	gint                       *tgt_parent;   /* per target bone */
	guint                      *tgt_order;    /* parents first */
	guint                      *src_order;
	LrgBonePose                *src_rest;     /* world rest poses */
	LrgBonePose                *tgt_rest;
	LrgBonePose                *tgt_bind;     /* local bind poses */
	LrgBonePose                *src_bind;
	gint                        root;         /* target root bone or -1 */
	gfloat                      scale_used;
};

enum
{
	RT_PROP_0,
	RT_PROP_SOURCE,
	RT_PROP_TARGET,
	RT_PROP_BONE_MAP,
	RT_PROP_TRANSLATION_MODE,
	RT_PROP_TRANSLATION_SCALE,
	RT_PROP_ROOT_BONE,
	RT_PROP_SAMPLE_RATE,
	RT_N_PROPS
};

static GParamSpec *rt_props[RT_N_PROPS];

G_DEFINE_FINAL_TYPE (LrgRetargeter, lrg_retargeter, G_TYPE_OBJECT)

static void
retargeter_free_state (LrgRetargeter *self)
{
	g_clear_pointer (&self->mapping, g_free);
	g_clear_pointer (&self->src_parent, g_free);
	g_clear_pointer (&self->tgt_parent, g_free);
	g_clear_pointer (&self->tgt_order, g_free);
	g_clear_pointer (&self->src_order, g_free);
	g_clear_pointer (&self->src_rest, g_free);
	g_clear_pointer (&self->tgt_rest, g_free);
	g_clear_pointer (&self->tgt_bind, g_free);
	g_clear_pointer (&self->src_bind, g_free);
}

static void
on_map_changed (LrgBoneMap *map,
                gpointer    user_data)
{
	LRG_RETARGETER (user_data)->dirty = TRUE;
}

static void
lrg_retargeter_dispose (GObject *object)
{
	LrgRetargeter *self = LRG_RETARGETER (object);

	if (self->map != NULL && self->map_handler != 0)
		g_signal_handler_disconnect (self->map, self->map_handler);
	self->map_handler = 0;
	g_clear_object (&self->map);
	g_clear_object (&self->source);
	g_clear_object (&self->target);
	G_OBJECT_CLASS (lrg_retargeter_parent_class)->dispose (object);
}

static void
lrg_retargeter_finalize (GObject *object)
{
	LrgRetargeter *self = LRG_RETARGETER (object);

	retargeter_free_state (self);
	g_free (self->root_bone);
	G_OBJECT_CLASS (lrg_retargeter_parent_class)->finalize (object);
}

static void
lrg_retargeter_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgRetargeter *self = LRG_RETARGETER (object);

	switch (prop_id)
	{
	case RT_PROP_SOURCE:
		g_value_set_object (value, self->source);
		break;
	case RT_PROP_TARGET:
		g_value_set_object (value, self->target);
		break;
	case RT_PROP_BONE_MAP:
		g_value_set_object (value, self->map);
		break;
	case RT_PROP_TRANSLATION_MODE:
		g_value_set_enum (value, self->translation_mode);
		break;
	case RT_PROP_TRANSLATION_SCALE:
		g_value_set_float (value, self->translation_scale);
		break;
	case RT_PROP_ROOT_BONE:
		g_value_set_string (value, self->root_bone);
		break;
	case RT_PROP_SAMPLE_RATE:
		g_value_set_float (value, self->sample_rate);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_retargeter_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgRetargeter *self = LRG_RETARGETER (object);

	switch (prop_id)
	{
	case RT_PROP_SOURCE:
		self->source = g_value_dup_object (value);
		break;
	case RT_PROP_TARGET:
		self->target = g_value_dup_object (value);
		break;
	case RT_PROP_BONE_MAP:
		self->map = g_value_dup_object (value);
		break;
	case RT_PROP_TRANSLATION_MODE:
		lrg_retargeter_set_translation_mode (self, g_value_get_enum (value));
		break;
	case RT_PROP_TRANSLATION_SCALE:
		lrg_retargeter_set_translation_scale (self, g_value_get_float (value));
		break;
	case RT_PROP_ROOT_BONE:
		lrg_retargeter_set_root_bone (self, g_value_get_string (value));
		break;
	case RT_PROP_SAMPLE_RATE:
		lrg_retargeter_set_sample_rate (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_retargeter_constructed (GObject *object)
{
	LrgRetargeter *self = LRG_RETARGETER (object);

	G_OBJECT_CLASS (lrg_retargeter_parent_class)->constructed (object);
	if (self->map == NULL)
		self->map = lrg_bone_map_new ();
	self->map_handler = g_signal_connect (self->map, "changed", G_CALLBACK (on_map_changed), self);
}

static void
lrg_retargeter_class_init (LrgRetargeterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_retargeter_dispose;
	object_class->finalize = lrg_retargeter_finalize;
	object_class->get_property = lrg_retargeter_get_property;
	object_class->set_property = lrg_retargeter_set_property;
	object_class->constructed = lrg_retargeter_constructed;

	/**
	 * LrgRetargeter:source:
	 *
	 * The skeleton the animation was made for (construct-only).
	 */
	rt_props[RT_PROP_SOURCE] =
		g_param_spec_object ("source", NULL, NULL, LRG_TYPE_SKELETON,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgRetargeter:target:
	 *
	 * The skeleton that gets animated (construct-only).
	 */
	rt_props[RT_PROP_TARGET] =
		g_param_spec_object ("target", NULL, NULL, LRG_TYPE_SKELETON,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgRetargeter:bone-map:
	 *
	 * The #LrgBoneMap pairing the bones (construct-only; a default one
	 * when not given).
	 */
	rt_props[RT_PROP_BONE_MAP] =
		g_param_spec_object ("bone-map", NULL, NULL, LRG_TYPE_BONE_MAP,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgRetargeter:translation-mode:
	 *
	 * Which target bones take translation from the source.
	 */
	rt_props[RT_PROP_TRANSLATION_MODE] =
		g_param_spec_enum ("translation-mode", NULL, NULL, LRG_TYPE_RETARGET_TRANSLATION_MODE,
		                   LRG_RETARGET_TRANSLATION_ROOT,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRetargeter:translation-scale:
	 *
	 * The factor applied to source movement; 0 picks it from the root
	 * bones' rest heights.
	 */
	rt_props[RT_PROP_TRANSLATION_SCALE] =
		g_param_spec_float ("translation-scale", NULL, NULL, 0.0f, G_MAXFLOAT, 0.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRetargeter:root-bone:
	 *
	 * The target bone that takes the source root's movement (%NULL: the
	 * topmost mapped bone).
	 */
	rt_props[RT_PROP_ROOT_BONE] =
		g_param_spec_string ("root-bone", NULL, NULL, NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgRetargeter:sample-rate:
	 *
	 * Keys per second written by lrg_retargeter_retarget_clip().
	 */
	rt_props[RT_PROP_SAMPLE_RATE] =
		g_param_spec_float ("sample-rate", NULL, NULL, 1.0f, 240.0f, 30.0f,
		                    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, RT_N_PROPS, rt_props);
}

static void
lrg_retargeter_init (LrgRetargeter *self)
{
	self->translation_mode = LRG_RETARGET_TRANSLATION_ROOT;
	self->translation_scale = 0.0f;
	self->sample_rate = 30.0f;
	self->dirty = TRUE;
	self->root = -1;
}

LrgRetargeter *
lrg_retargeter_new (LrgSkeleton *source,
                    LrgSkeleton *target,
                    LrgBoneMap  *map)
{
	g_return_val_if_fail (LRG_IS_SKELETON (source), NULL);
	g_return_val_if_fail (LRG_IS_SKELETON (target), NULL);
	g_return_val_if_fail (map == NULL || LRG_IS_BONE_MAP (map), NULL);

	return g_object_new (LRG_TYPE_RETARGETER, "source", source, "target", target, "bone-map", map, NULL);
}

LrgSkeleton *
lrg_retargeter_get_source (LrgRetargeter *self)
{
	g_return_val_if_fail (LRG_IS_RETARGETER (self), NULL);
	return self->source;
}

LrgSkeleton *
lrg_retargeter_get_target (LrgRetargeter *self)
{
	g_return_val_if_fail (LRG_IS_RETARGETER (self), NULL);
	return self->target;
}

LrgBoneMap *
lrg_retargeter_get_bone_map (LrgRetargeter *self)
{
	g_return_val_if_fail (LRG_IS_RETARGETER (self), NULL);
	return self->map;
}

void
lrg_retargeter_set_translation_mode (LrgRetargeter              *self,
                                     LrgRetargetTranslationMode  mode)
{
	g_return_if_fail (LRG_IS_RETARGETER (self));
	if (self->translation_mode == mode)
		return;
	self->translation_mode = mode;
	g_object_notify_by_pspec (G_OBJECT (self), rt_props[RT_PROP_TRANSLATION_MODE]);
}

LrgRetargetTranslationMode
lrg_retargeter_get_translation_mode (LrgRetargeter *self)
{
	g_return_val_if_fail (LRG_IS_RETARGETER (self), LRG_RETARGET_TRANSLATION_ROOT);
	return self->translation_mode;
}

void
lrg_retargeter_set_translation_scale (LrgRetargeter *self,
                                      gfloat         scale)
{
	g_return_if_fail (LRG_IS_RETARGETER (self));
	scale = isfinite (scale) && scale > 0.0f ? scale : 0.0f;
	if (self->translation_scale == scale)
		return;
	self->translation_scale = scale;
	self->dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), rt_props[RT_PROP_TRANSLATION_SCALE]);
}

void
lrg_retargeter_set_root_bone (LrgRetargeter *self,
                              const gchar   *target_bone)
{
	g_return_if_fail (LRG_IS_RETARGETER (self));
	if (g_strcmp0 (self->root_bone, target_bone) == 0)
		return;
	g_free (self->root_bone);
	self->root_bone = g_strdup (target_bone);
	self->dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), rt_props[RT_PROP_ROOT_BONE]);
}

void
lrg_retargeter_set_sample_rate (LrgRetargeter *self,
                                gfloat         fps)
{
	g_return_if_fail (LRG_IS_RETARGETER (self));
	fps = CLAMP (isfinite (fps) ? fps : 30.0f, 1.0f, 240.0f);
	if (self->sample_rate == fps)
		return;
	self->sample_rate = fps;
	g_object_notify_by_pspec (G_OBJECT (self), rt_props[RT_PROP_SAMPLE_RATE]);
}

gfloat
lrg_retargeter_get_sample_rate (LrgRetargeter *self)
{
	g_return_val_if_fail (LRG_IS_RETARGETER (self), 30.0f);
	return self->sample_rate;
}

/* parents-first order of a skeleton with indices 0 .. n-1 */
static guint *
parent_first_order (const gint *parent,
                    guint       n)
{
	guint            *order = g_new (guint, MAX (n, 1));
	g_autofree guint8 *done = g_new0 (guint8, MAX (n, 1));
	guint             count = 0;
	guint             pass;
	guint             i;

	for (pass = 0; pass <= n && count < n; pass++)
		for (i = 0; i < n; i++)
		{
			gint p = parent[i];

			if (done[i])
				continue;
			if (p < 0 || (guint)p >= n || p == (gint)i || done[p] || pass == n)
			{
				order[count++] = i;
				done[i] = 1;
			}
		}
	return order;
}

/*
 * retargeter_resolve:
 * (Re)builds the pairing, rest poses and orders after a change.
 */
static gboolean
retargeter_resolve (LrgRetargeter *self)
{
	guint i;

	if (!self->dirty)
		return self->ok;
	retargeter_free_state (self);
	self->dirty = FALSE;
	self->ok = FALSE;
	self->root = -1;
	self->ns = lrg_skeleton_get_bone_count (self->source);
	self->nt = lrg_skeleton_get_bone_count (self->target);
	if (self->ns == 0 || self->nt == 0)
		return FALSE;

	self->src_rest = g_new (LrgBonePose, self->ns);
	self->tgt_rest = g_new (LrgBonePose, self->nt);
	if (!lrg_skeleton_compute_bind_world_poses (self->source, self->src_rest, self->ns) ||
	    !lrg_skeleton_compute_bind_world_poses (self->target, self->tgt_rest, self->nt))
		return FALSE;

	self->src_parent = g_new (gint, self->ns);
	self->src_bind = g_new (LrgBonePose, self->ns);
	for (i = 0; i < self->ns; i++)
	{
		LrgBone *b = lrg_skeleton_get_bone (self->source, (gint)i);

		self->src_parent[i] = lrg_bone_get_parent_index (b);
		self->src_bind[i] = *lrg_bone_get_bind_pose (b);
	}
	self->tgt_parent = g_new (gint, self->nt);
	self->tgt_bind = g_new (LrgBonePose, self->nt);
	for (i = 0; i < self->nt; i++)
	{
		LrgBone *b = lrg_skeleton_get_bone (self->target, (gint)i);

		self->tgt_parent[i] = lrg_bone_get_parent_index (b);
		self->tgt_bind[i] = *lrg_bone_get_bind_pose (b);
	}
	self->src_order = parent_first_order (self->src_parent, self->ns);
	self->tgt_order = parent_first_order (self->tgt_parent, self->nt);
	self->mapping = lrg_bone_map_resolve (self->map, self->source, self->target, NULL);

	/* root: the named bone, else the first mapped bone parents-first */
	if (self->root_bone != NULL)
	{
		LrgBone *b = lrg_skeleton_get_bone_by_name (self->target, self->root_bone);

		if (b != NULL && self->mapping[lrg_bone_get_index (b)] >= 0)
			self->root = lrg_bone_get_index (b);
	}
	for (i = 0; i < self->nt && self->root < 0; i++)
		if (self->mapping[self->tgt_order[i]] >= 0)
			self->root = (gint)self->tgt_order[i];
	if (self->root < 0)
		return FALSE;

	/* translation scale: explicit, else the root bones' height ratio */
	self->scale_used = self->translation_scale;
	if (self->scale_used <= 0.0f)
	{
		gfloat hs = fabsf (self->src_rest[self->mapping[self->root]].position_y);
		gfloat ht = fabsf (self->tgt_rest[self->root].position_y);

		self->scale_used = hs > 1e-6f && ht > 1e-6f ? ht / hs : 1.0f;
	}
	self->ok = TRUE;
	return TRUE;
}

gfloat
lrg_retargeter_get_translation_scale (LrgRetargeter *self)
{
	g_return_val_if_fail (LRG_IS_RETARGETER (self), 1.0f);
	if (self->translation_scale > 0.0f)
		return self->translation_scale;
	return retargeter_resolve (self) ? self->scale_used : 1.0f;
}

const gchar *
lrg_retargeter_get_root_bone (LrgRetargeter *self)
{
	LrgBone *b;

	g_return_val_if_fail (LRG_IS_RETARGETER (self), NULL);
	if (!retargeter_resolve (self))
		return NULL;
	b = lrg_skeleton_get_bone (self->target, self->root);
	return b != NULL ? lrg_bone_get_name (b) : NULL;
}

const gint *
lrg_retargeter_get_mapping (LrgRetargeter *self,
                            guint         *n_target)
{
	g_return_val_if_fail (LRG_IS_RETARGETER (self), NULL);
	retargeter_resolve (self);
	if (n_target != NULL)
		*n_target = self->mapping != NULL ? self->nt : 0;
	return self->mapping;
}

/* does target bone t take translation under the current mode? */
static gboolean
moves (LrgRetargeter *self,
       guint          t)
{
	switch (self->translation_mode)
	{
	case LRG_RETARGET_TRANSLATION_ALL:
		return self->mapping[t] >= 0;
	case LRG_RETARGET_TRANSLATION_ROOT:
		return (gint)t == self->root;
	case LRG_RETARGET_TRANSLATION_NONE:
	default:
		return FALSE;
	}
}

gboolean
lrg_retargeter_retarget_pose (LrgRetargeter     *self,
                              const LrgBonePose *source_local,
                              guint              n_source,
                              LrgBonePose       *out_target_local,
                              guint              n_target)
{
	g_autofree LrgBonePose *src_world = NULL;
	g_autofree LrgBonePose *tgt_world = NULL;
	guint                   k;

	g_return_val_if_fail (LRG_IS_RETARGETER (self), FALSE);
	g_return_val_if_fail (source_local != NULL && out_target_local != NULL, FALSE);

	if (!retargeter_resolve (self) || n_source != self->ns || n_target != self->nt)
		return FALSE;

	/* 1. source forward kinematics */
	src_world = g_new (LrgBonePose, self->ns);
	for (k = 0; k < self->ns; k++)
	{
		guint s = self->src_order[k];
		gint  p = self->src_parent[s];

		if (p < 0 || (guint)p >= self->ns || p == (gint)s)
			src_world[s] = source_local[s];
		else
		{
			g_autoptr(LrgBonePose) w = lrg_bone_pose_compose (&src_world[p], &source_local[s]);

			src_world[s] = *w;
		}
	}

	/* 2. target, parents first */
	tgt_world = g_new (LrgBonePose, self->nt);
	for (k = 0; k < self->nt; k++)
	{
		guint        t = self->tgt_order[k];
		gint         p = self->tgt_parent[t];
		gint         s = self->mapping[t];
		gboolean     has_parent = p >= 0 && (guint)p < self->nt && p != (gint)t;
		LrgBonePose  local = self->tgt_bind[t];
		gfloat       pr[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		gfloat       pr_inv[4];

		if (has_parent)
			pose_rot (&tgt_world[p], pr);
		q_conj (pr, pr_inv);

		if (s >= 0)
		{
			gfloat ws[4];
			gfloat sr[4];
			gfloat sr_inv[4];
			gfloat tr[4];
			gfloat d[4];
			gfloat wt[4];
			gfloat lr[4];

			/* rotation: D = Ws Sr^-1, Wt = D Tr, local = Wp^-1 Wt */
			pose_rot (&src_world[s], ws);
			pose_rot (&self->src_rest[s], sr);
			pose_rot (&self->tgt_rest[t], tr);
			q_conj (sr, sr_inv);
			q_mul (ws, sr_inv, d);
			q_mul (d, tr, wt);
			q_mul (pr_inv, wt, lr);
			q_norm (lr);
			local.rotation_x = lr[0];
			local.rotation_y = lr[1];
			local.rotation_z = lr[2];
			local.rotation_w = lr[3];

			/* translation: rest world position + scaled source displacement */
			if (moves (self, t))
			{
				gfloat world[3];

				world[0] = self->tgt_rest[t].position_x +
				           self->scale_used * (src_world[s].position_x - self->src_rest[s].position_x);
				world[1] = self->tgt_rest[t].position_y +
				           self->scale_used * (src_world[s].position_y - self->src_rest[s].position_y);
				world[2] = self->tgt_rest[t].position_z +
				           self->scale_used * (src_world[s].position_z - self->src_rest[s].position_z);
				if (has_parent)
				{
					const LrgBonePose *pw = &tgt_world[p];
					gfloat             rel[3];
					gfloat             lp[3];

					rel[0] = world[0] - pw->position_x;
					rel[1] = world[1] - pw->position_y;
					rel[2] = world[2] - pw->position_z;
					q_rotate (pr_inv, rel, lp);
					local.position_x = pw->scale_x != 0.0f ? lp[0] / pw->scale_x : lp[0];
					local.position_y = pw->scale_y != 0.0f ? lp[1] / pw->scale_y : lp[1];
					local.position_z = pw->scale_z != 0.0f ? lp[2] / pw->scale_z : lp[2];
				}
				else
				{
					local.position_x = world[0];
					local.position_y = world[1];
					local.position_z = world[2];
				}
			}
		}

		out_target_local[t] = local;
		if (has_parent)
		{
			g_autoptr(LrgBonePose) w = lrg_bone_pose_compose (&tgt_world[p], &local);

			tgt_world[t] = *w;
		}
		else
			tgt_world[t] = local;
	}
	return TRUE;
}

gboolean
lrg_retargeter_retarget_skeleton (LrgRetargeter *self)
{
	g_autofree LrgBonePose *src = NULL;
	g_autofree LrgBonePose *dst = NULL;
	guint                   i;

	g_return_val_if_fail (LRG_IS_RETARGETER (self), FALSE);

	if (!retargeter_resolve (self))
		return FALSE;
	src = g_new (LrgBonePose, self->ns);
	dst = g_new (LrgBonePose, self->nt);
	for (i = 0; i < self->ns; i++)
		src[i] = *lrg_bone_get_local_pose (lrg_skeleton_get_bone (self->source, (gint)i));
	if (!lrg_retargeter_retarget_pose (self, src, self->ns, dst, self->nt))
		return FALSE;
	for (i = 0; i < self->nt; i++)
		lrg_skeleton_set_pose (self->target, (gint)i, &dst[i]);
	lrg_skeleton_calculate_world_poses (self->target);
	return TRUE;
}

LrgAnimationClip *
lrg_retargeter_retarget_clip (LrgRetargeter     *self,
                              LrgAnimationClip  *clip,
                              GError           **error)
{
	g_autoptr(LrgAnimationClip) out = NULL;
	g_autofree gint            *src_track = NULL;   /* per source bone: clip track or -1 */
	g_autofree LrgBonePose     *src = NULL;
	g_autofree LrgBonePose     *dst = NULL;
	g_autofree gfloat          *times = NULL;
	g_autofree gfloat          *rot = NULL;          /* per target bone: 4 per sample */
	g_autofree gfloat          *pos = NULL;          /* per target bone: 3 per sample */
	gfloat                      duration;
	guint                       n_samples;
	guint                       i;
	guint                       k;
	gchar                      *name;

	g_return_val_if_fail (LRG_IS_RETARGETER (self), NULL);
	g_return_val_if_fail (LRG_IS_ANIMATION_CLIP (clip), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!retargeter_resolve (self))
	{
		g_set_error_literal (error, LRG_RETARGET_ERROR, LRG_RETARGET_ERROR_SKELETON,
		                     "Retargeting needs skeletons with bone indices 0 .. count - 1 and at least one mapped bone");
		return NULL;
	}

	/* which clip track drives each source bone */
	src_track = g_new (gint, self->ns);
	for (i = 0; i < self->ns; i++)
		src_track[i] = lrg_animation_clip_find_track (clip,
		                                              lrg_bone_get_name (lrg_skeleton_get_bone (self->source, (gint)i)));

	/* sample times: 0, 1/rate, ... and the last instant */
	duration = lrg_animation_clip_get_duration (clip);
	n_samples = (guint)floorf (duration * self->sample_rate + 1e-4f) + 1;
	if ((gfloat)(n_samples - 1) / self->sample_rate < duration - 1e-5f)
		n_samples++;
	times = g_new (gfloat, n_samples);
	for (k = 0; k < n_samples; k++)
		times[k] = MIN ((gfloat)k / self->sample_rate, duration);

	src = g_new (LrgBonePose, self->ns);
	dst = g_new (LrgBonePose, self->nt);
	rot = g_new (gfloat, (gsize)self->nt * n_samples * 4);
	pos = g_new (gfloat, (gsize)self->nt * n_samples * 3);
	for (k = 0; k < n_samples; k++)
	{
		/* the source pose: animated bones from the clip, the rest bind.
		 * Sampled through the track itself so loop modes do not wrap the
		 * last instant back to 0. */
		for (i = 0; i < self->ns; i++)
		{
			if (src_track[i] >= 0)
				lrg_animation_clip_sample_track (clip, (guint)src_track[i],
				                                 k + 1 == n_samples ? duration - 1e-6f : times[k], &src[i]);
			else
				src[i] = self->src_bind[i];
		}
		lrg_retargeter_retarget_pose (self, src, self->ns, dst, self->nt);
		for (i = 0; i < self->nt; i++)
		{
			gfloat *r = rot + ((gsize)i * n_samples + k) * 4;
			gfloat *p = pos + ((gsize)i * n_samples + k) * 3;

			r[0] = dst[i].rotation_x;
			r[1] = dst[i].rotation_y;
			r[2] = dst[i].rotation_z;
			r[3] = dst[i].rotation_w;
			/* keep consecutive keys in one hemisphere for clean curves */
			if (k > 0)
			{
				gfloat *q0 = r - 4;

				if (q0[0] * r[0] + q0[1] * r[1] + q0[2] * r[2] + q0[3] * r[3] < 0.0f)
				{
					r[0] = -r[0];
					r[1] = -r[1];
					r[2] = -r[2];
					r[3] = -r[3];
				}
			}
			p[0] = dst[i].position_x;
			p[1] = dst[i].position_y;
			p[2] = dst[i].position_z;
		}
	}

	/* the baked clip */
	name = g_strdup (lrg_animation_clip_get_name (clip));
	out = lrg_animation_clip_new (name);
	g_free (name);
	lrg_animation_clip_set_loop_mode (out, lrg_animation_clip_get_loop_mode (clip));
	for (i = 0; i < self->nt; i++)
	{
		guint track;

		if (self->mapping[i] < 0)
			continue;
		track = lrg_animation_clip_add_track (out, lrg_bone_get_name (lrg_skeleton_get_bone (self->target, (gint)i)));
		lrg_animation_clip_set_track_base_pose (out, track, &self->tgt_bind[i]);
		if (!lrg_animation_clip_set_track_channel (out, track, LRG_ANIMATION_CHANNEL_ROTATION,
		                                           LRG_ANIMATION_INTERPOLATION_LINEAR, n_samples, times,
		                                           rot + (gsize)i * n_samples * 4, error))
			return NULL;
		if (moves (self, i) &&
		    !lrg_animation_clip_set_track_channel (out, track, LRG_ANIMATION_CHANNEL_TRANSLATION,
		                                           LRG_ANIMATION_INTERPOLATION_LINEAR, n_samples, times,
		                                           pos + (gsize)i * n_samples * 3, error))
			return NULL;
	}

	/* morph weights travel unchanged, sampled the same way */
	for (i = 0; i < lrg_animation_clip_get_weights_channel_count (clip); i++)
	{
		guint              w = lrg_animation_clip_get_weights_channel_width (clip, i);
		g_autofree gfloat *values = g_new (gfloat, (gsize)w * n_samples);

		for (k = 0; k < n_samples; k++)
			lrg_animation_clip_sample_weights (clip, i, k + 1 == n_samples ? duration - 1e-6f : times[k],
			                                   values + (gsize)k * w);
		if (lrg_animation_clip_add_weights_channel (out, lrg_animation_clip_get_weights_channel_target (clip, i),
		                                            w, LRG_ANIMATION_INTERPOLATION_LINEAR, n_samples, times,
		                                            values, error) < 0)
			return NULL;
	}
	for (i = 0; i < lrg_animation_clip_get_event_count (clip); i++)
		lrg_animation_clip_add_event (out, lrg_animation_clip_get_event (clip, i));
	lrg_animation_clip_set_duration (out, duration);
	return g_steal_pointer (&out);
}
