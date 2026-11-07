/* lrg-level-serializer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * YAML (.rlevel) serializer for #LrgLevel documents.
 */

#include "lrg-level-serializer.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include "lrg-node-visual.h"
#include "lrg-node-physics.h"
#include "lrg-node-tilemap.h"
#include "lrg-timeline.h"
#include "lrg-component-desc.h"
#include "lrg-script-binding.h"
#include "lrg-prefab.h"
#include "../scene/lrg-material3d.h"
#include "../core/lrg-asset-manager.h"
#include "../lrg-enums.h"
#include "../ui/lrg-property-source.h"
#include <graylib.h>
#include <yaml-glib.h>
#include "../lighting/lrg-environment-glsl-private.h"
#include <string.h>

/* The level / project key naming how linked instances are stored. The
 * hyphenated spelling is accepted on read. */
#define STORAGE_KEY      "store_prefab_instances"
#define STORAGE_KEY_ALT  "store-prefab-instances"
#define PROJECT_MANIFEST "project.ryaml"

struct _LrgLevelSerializer
{
	GObject           parent_instance;

	LrgPrefabStorage  prefab_storage;   /* DEFAULT: level, then project */
	gchar            *project_root;     /* nullable; else discovered */
	LrgAssetManager  *asset_manager;    /* nullable; resolves guid: */
};

G_DEFINE_FINAL_TYPE (LrgLevelSerializer, lrg_level_serializer, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_PREFAB_STORAGE,
	PROP_PROJECT_ROOT,
	PROP_ASSET_MANAGER,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_level_serializer_finalize (GObject *object)
{
	LrgLevelSerializer *self = LRG_LEVEL_SERIALIZER (object);

	g_clear_pointer (&self->project_root, g_free);
	g_clear_object (&self->asset_manager);

	G_OBJECT_CLASS (lrg_level_serializer_parent_class)->finalize (object);
}

static void
lrg_level_serializer_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
	LrgLevelSerializer *self = LRG_LEVEL_SERIALIZER (object);

	switch (prop_id)
	{
	case PROP_PREFAB_STORAGE:
		g_value_set_enum (value, self->prefab_storage);
		break;
	case PROP_PROJECT_ROOT:
		g_value_set_string (value, self->project_root);
		break;
	case PROP_ASSET_MANAGER:
		g_value_set_object (value, self->asset_manager);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_level_serializer_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
	LrgLevelSerializer *self = LRG_LEVEL_SERIALIZER (object);

	switch (prop_id)
	{
	case PROP_PREFAB_STORAGE:
		lrg_level_serializer_set_prefab_storage (self, (LrgPrefabStorage) g_value_get_enum (value));
		break;
	case PROP_PROJECT_ROOT:
		lrg_level_serializer_set_project_root (self, g_value_get_string (value));
		break;
	case PROP_ASSET_MANAGER:
		lrg_level_serializer_set_asset_manager (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_level_serializer_class_init (LrgLevelSerializerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_level_serializer_finalize;
	object_class->get_property = lrg_level_serializer_get_property;
	object_class->set_property = lrg_level_serializer_set_property;

	/**
	 * LrgLevelSerializer:prefab-storage:
	 *
	 * How saving stores linked prefab instances. %LRG_PREFAB_STORAGE_DEFAULT
	 * (the default) defers to the level's own setting
	 * (lrg_prefab_level_get_storage()), then to `store_prefab_instances` in
	 * the project's `project.ryaml`, then to
	 * %LRG_PREFAB_STORAGE_EXPANDED. Loading always reads both forms.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_PREFAB_STORAGE] =
		g_param_spec_enum ("prefab-storage", "Prefab storage",
		                   "How linked prefab instances are saved",
		                   LRG_TYPE_PREFAB_STORAGE, LRG_PREFAB_STORAGE_DEFAULT,
		                   G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgLevelSerializer:project-root:
	 *
	 * The project directory, used to find `project.ryaml` (the project
	 * `store_prefab_instances` setting) and to resolve project-relative
	 * prefab paths of reference-stored instances. When %NULL, file loads
	 * and saves look for `project.ryaml` in the level's directory and its
	 * parents.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_PROJECT_ROOT] =
		g_param_spec_string ("project-root", "Project root",
		                     "Project directory (holding project.ryaml)",
		                     NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgLevelSerializer:asset-manager:
	 *
	 * An optional #LrgAssetManager whose guid index resolves the prefab
	 * sources of reference-stored instances (by the link's asset guid, or
	 * a `guid:` source path) before any path is tried. Without one, guid
	 * sources are resolved through a guid index of the project root, when
	 * one is known.
	 *
	 * Since: 0.9.0
	 */
	properties[PROP_ASSET_MANAGER] =
		g_param_spec_object ("asset-manager", "Asset manager",
		                     "Resolves guid prefab sources",
		                     LRG_TYPE_ASSET_MANAGER,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_level_serializer_init (LrgLevelSerializer *self)
{
	self->prefab_storage = LRG_PREFAB_STORAGE_DEFAULT;
}

/* ==========================================================================
 * Project settings
 * ========================================================================== */

/*
 * find_project_root:
 * @start: (nullable): a directory
 *
 * Walks up from @start to the first directory holding project.ryaml.
 */
static gchar *
find_project_root (const gchar *start)
{
	g_autofree gchar *dir = NULL;
	guint             depth;

	if (start == NULL)
		return NULL;
	dir = g_canonicalize_filename (start, NULL);
	for (depth = 0; depth < 64 && dir != NULL; depth++)
	{
		g_autofree gchar *manifest = g_build_filename (dir, PROJECT_MANIFEST, NULL);
		g_autofree gchar *parent = NULL;

		if (g_file_test (manifest, G_FILE_TEST_IS_REGULAR))
			return g_steal_pointer (&dir);
		parent = g_path_get_dirname (dir);
		if (g_strcmp0 (parent, dir) == 0)
			break;
		g_free (dir);
		dir = g_steal_pointer (&parent);
	}
	return NULL;
}

/* The serializer's project root, or one discovered above @level_path. */
static gchar *
effective_project_root (LrgLevelSerializer *self,
                        const gchar        *level_path)
{
	g_autofree gchar *dir = NULL;

	if (self->project_root != NULL)
		return g_strdup (self->project_root);
	if (level_path == NULL)
		return NULL;
	dir = g_path_get_dirname (level_path);
	return find_project_root (dir);
}

/* Reads `project.store_prefab_instances` from <root>/project.ryaml. */
static LrgPrefabStorage
project_storage (const gchar *root)
{
	g_autofree gchar      *path = NULL;
	g_autoptr(YamlParser)  parser = NULL;
	YamlNode              *doc;
	YamlNode              *proj;
	YamlMapping           *map;
	const gchar           *text = NULL;

	if (root == NULL)
		return LRG_PREFAB_STORAGE_DEFAULT;
	path = g_build_filename (root, PROJECT_MANIFEST, NULL);
	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, path, NULL))
		return LRG_PREFAB_STORAGE_DEFAULT;
	doc = yaml_parser_get_root (parser);
	if (doc == NULL || yaml_node_get_node_type (doc) != YAML_NODE_MAPPING)
		return LRG_PREFAB_STORAGE_DEFAULT;
	proj = yaml_mapping_get_member (yaml_node_get_mapping (doc), "project");
	if (proj == NULL || yaml_node_get_node_type (proj) != YAML_NODE_MAPPING)
		return LRG_PREFAB_STORAGE_DEFAULT;
	map = yaml_node_get_mapping (proj);
	if (yaml_mapping_has_member (map, STORAGE_KEY))
		text = yaml_mapping_get_string_member (map, STORAGE_KEY);
	else if (yaml_mapping_has_member (map, STORAGE_KEY_ALT))
		text = yaml_mapping_get_string_member (map, STORAGE_KEY_ALT);
	return lrg_prefab_storage_from_string (text);
}

/*
 * effective_storage:
 *
 * Serializer property, then the level setting, then the project, then
 * expanded (the 0.8.x format).
 */
static LrgPrefabStorage
effective_storage (LrgLevelSerializer *self,
                   LrgLevel           *level,
                   const gchar        *level_path)
{
	LrgPrefabStorage  storage = self->prefab_storage;
	g_autofree gchar *root = NULL;

	if (storage == LRG_PREFAB_STORAGE_DEFAULT)
		storage = lrg_prefab_level_get_storage (level);
	if (storage == LRG_PREFAB_STORAGE_DEFAULT)
	{
		root = effective_project_root (self, level_path);
		storage = project_storage (root);
	}
	if (storage == LRG_PREFAB_STORAGE_DEFAULT)
		storage = LRG_PREFAB_STORAGE_EXPANDED;
	return storage;
}

static gchar *save_to_data_internal (LrgLevelSerializer *self, LrgLevel *level,
                                     const gchar *path, gsize *length);

/* ==========================================================================
 * Enum <-> nick helpers
 * ========================================================================== */

static const gchar *
enum_to_nick (GType enum_type, gint value)
{
	g_autoptr(GEnumClass)  klass = g_type_class_ref (enum_type);
	GEnumValue            *ev = g_enum_get_value (klass, value);

	return (ev != NULL) ? ev->value_nick : NULL;
}

static gint
nick_to_enum (GType enum_type, const gchar *nick, gint fallback)
{
	g_autoptr(GEnumClass)  klass = NULL;
	GEnumValue            *ev;

	if (nick == NULL)
		return fallback;

	klass = g_type_class_ref (enum_type);
	ev = g_enum_get_value_by_nick (klass, nick);

	return (ev != NULL) ? ev->value : fallback;
}

/* ==========================================================================
 * GValue <-> YAML scalar helpers
 * ========================================================================== */

/* Store a #GValue as a typed YAML scalar member. */
static void
set_value_member (YamlMapping  *map,
                  const gchar  *key,
                  const GValue *value)
{
	GType vtype = G_VALUE_TYPE (value);

	switch (vtype)
	{
	case G_TYPE_STRING:
		yaml_mapping_set_string_member (map, key, g_value_get_string (value));
		break;
	case G_TYPE_BOOLEAN:
		yaml_mapping_set_boolean_member (map, key, g_value_get_boolean (value));
		break;
	case G_TYPE_INT:
		yaml_mapping_set_int_member (map, key, (gint64) g_value_get_int (value));
		break;
	case G_TYPE_UINT:
		yaml_mapping_set_int_member (map, key, (gint64) g_value_get_uint (value));
		break;
	case G_TYPE_INT64:
		yaml_mapping_set_int_member (map, key, g_value_get_int64 (value));
		break;
	case G_TYPE_FLOAT:
		yaml_mapping_set_double_member (map, key, (gdouble) g_value_get_float (value));
		break;
	case G_TYPE_DOUBLE:
		yaml_mapping_set_double_member (map, key, g_value_get_double (value));
		break;
	default:
		if (G_TYPE_IS_ENUM (vtype) || G_TYPE_IS_FLAGS (vtype))
		{
			/* Enums and flags as their nicks ("melee", "fire|ice"):
			 * readable, stable across renumbering, and parsed back
			 * by lrg_property_value_from_string() when the level is
			 * instantiated (GLib's own transform writes the C value
			 * name, which nothing reads back). */
			g_autofree gchar *nick = lrg_property_value_to_string (value);

			yaml_mapping_set_string_member (map, key, nick);
			break;
		}
		{
			/* Fall back to a transformed string representation. */
			GValue strv = G_VALUE_INIT;

			g_value_init (&strv, G_TYPE_STRING);
			if (g_value_transform (value, &strv))
				yaml_mapping_set_string_member (map, key, g_value_get_string (&strv));
			else
			{
				g_autofree gchar *repr = g_strdup_value_contents (value);
				yaml_mapping_set_string_member (map, key, repr);
			}
			g_value_unset (&strv);
		}
		break;
	}
}

/* Heuristically decode a YAML scalar string into a typed #GValue. */
static void
value_from_scalar (const gchar *s,
                   GValue      *out)
{
	gchar  *end = NULL;
	gint64  iv;
	gdouble dv;

	if (s == NULL)
	{
		g_value_init (out, G_TYPE_STRING);
		g_value_set_string (out, NULL);
		return;
	}

	if (g_ascii_strcasecmp (s, "true") == 0)
	{
		g_value_init (out, G_TYPE_BOOLEAN);
		g_value_set_boolean (out, TRUE);
		return;
	}
	if (g_ascii_strcasecmp (s, "false") == 0)
	{
		g_value_init (out, G_TYPE_BOOLEAN);
		g_value_set_boolean (out, FALSE);
		return;
	}

	iv = g_ascii_strtoll (s, &end, 10);
	if (s[0] != '\0' && end != NULL && *end == '\0')
	{
		g_value_init (out, G_TYPE_INT64);
		g_value_set_int64 (out, iv);
		return;
	}

	dv = g_ascii_strtod (s, &end);
	if (s[0] != '\0' && end != NULL && *end == '\0')
	{
		g_value_init (out, G_TYPE_DOUBLE);
		g_value_set_double (out, dv);
		return;
	}

	g_value_init (out, G_TYPE_STRING);
	g_value_set_string (out, s);
}

static gdouble
seq_get_double (YamlSequence *seq,
                guint         index,
                gdouble       fallback)
{
	YamlNode    *node;
	const gchar *scalar;

	if (seq == NULL || index >= yaml_sequence_get_length (seq))
		return fallback;

	node = yaml_sequence_get_element (seq, index);
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return fallback;

	scalar = yaml_node_get_scalar (node);
	if (scalar == NULL)
		return fallback;

	return g_ascii_strtod (scalar, NULL);
}

/* ==========================================================================
 * The `lighting:` block (0.9.0 M3)
 *
 * lighting:
 *   lightmap: lightmaps/level.rlightmap     # path (relative to the level) or guid:...
 *   probes: [lightmaps/hall.rprobes, guid:...]
 *   settings: { texels_per_unit: 8, samples: 256, bounces: 2,
 *               max_lightmap_size: 1024, denoise: 1, probe_spacing: 2 }
 * ========================================================================== */

/* the `lighting:` mapping of @level */
static YamlMapping *
build_lighting_mapping (LrgLevel *level)
{
	YamlMapping          *map = yaml_mapping_new ();
	GPtrArray            *probes = lrg_level_get_probe_refs (level);
	g_autoptr(GPtrArray)  keys = lrg_level_get_bake_setting_keys (level);
	guint                 i;

	if (lrg_level_get_lightmap_ref (level) != NULL)
		yaml_mapping_set_string_member (map, "lightmap", lrg_level_get_lightmap_ref (level));
	if (probes->len > 0)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();

		for (i = 0; i < probes->len; i++)
			yaml_sequence_add_string_element (seq, g_ptr_array_index (probes, i));
		yaml_mapping_set_sequence_member (map, "probes", seq);
	}
	if (keys->len > 0)
	{
		g_autoptr(YamlMapping) settings = yaml_mapping_new ();

		for (i = 0; i < keys->len; i++)
		{
			const gchar         *key = g_ptr_array_index (keys, i);
			gdouble              v = 0.0;
			gchar                buf[G_ASCII_DTOSTR_BUF_SIZE];
			g_autoptr(YamlNode)  n = NULL;

			lrg_level_get_bake_setting (level, key, &v);
			n = yaml_node_new_scalar (g_ascii_formatd (buf, sizeof buf, "%.9g", v), YAML_SCALAR_STYLE_PLAIN);
			yaml_mapping_set_member (settings, key, n);
		}
		yaml_mapping_set_mapping_member (map, "settings", settings);
	}
	return map;
}

/* a scalar as a number: numbers, and true / yes / false / no as 1 / 0 */
static gboolean
lighting_number (YamlNode *node,
                 gdouble  *out)
{
	const gchar *s;
	gchar       *end = NULL;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return FALSE;
	s = yaml_node_get_scalar (node);
	if (s == NULL)
		return FALSE;
	if (g_ascii_strcasecmp (s, "true") == 0 || g_ascii_strcasecmp (s, "yes") == 0)
	{
		*out = 1.0;
		return TRUE;
	}
	if (g_ascii_strcasecmp (s, "false") == 0 || g_ascii_strcasecmp (s, "no") == 0)
	{
		*out = 0.0;
		return TRUE;
	}
	*out = g_ascii_strtod (s, &end);
	return end != s && *end == '\0';
}

/* reads a `lighting:` node into @level; FALSE with @error when malformed */
static gboolean
parse_lighting (LrgLevel  *level,
                YamlNode  *node,
                GError   **error)
{
	YamlMapping *map;
	YamlNode    *member;

	if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE, "must be a mapping");
		return FALSE;
	}
	map = yaml_node_get_mapping (node);

	member = yaml_mapping_get_member (map, "lightmap");
	if (member != NULL && !yaml_node_is_null (member))
	{
		if (yaml_node_get_node_type (member) != YAML_NODE_SCALAR)
		{
			g_set_error_literal (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
			                     "lightmap must be a path or guid: reference");
			return FALSE;
		}
		lrg_level_set_lightmap_ref (level, yaml_node_get_scalar (member));
	}

	member = yaml_mapping_get_member (map, "probes");
	if (member != NULL && !yaml_node_is_null (member))
	{
		YamlSequence *seq;
		guint         i, n;

		if (yaml_node_get_node_type (member) != YAML_NODE_SEQUENCE)
		{
			g_set_error_literal (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
			                     "probes must be a sequence of paths or guid: references");
			return FALSE;
		}
		seq = yaml_node_get_sequence (member);
		n = yaml_sequence_get_length (seq);
		for (i = 0; i < n; i++)
		{
			const gchar *ref = yaml_sequence_get_string_element (seq, i);

			if (ref == NULL || *ref == '\0')
			{
				g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
				             "probes[%u] is not a path or guid: reference", i);
				return FALSE;
			}
			lrg_level_add_probe_ref (level, ref);
		}
	}

	member = yaml_mapping_get_member (map, "settings");
	if (member != NULL && !yaml_node_is_null (member))
	{
		YamlMapping      *smap;
		g_autoptr(GList)  keys = NULL;
		GList            *l;

		if (yaml_node_get_node_type (member) != YAML_NODE_MAPPING)
		{
			g_set_error_literal (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE, "settings must be a mapping");
			return FALSE;
		}
		smap = yaml_node_get_mapping (member);
		keys = yaml_mapping_get_members (smap);
		for (l = keys; l != NULL; l = l->next)
		{
			gdouble v;

			if (!lighting_number (yaml_mapping_get_member (smap, l->data), &v))
			{
				g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
				             "settings.%s must be a number or a boolean", (const gchar *)l->data);
				return FALSE;
			}
			lrg_level_set_bake_setting (level, l->data, v);
		}
	}
	return TRUE;
}

/* ==========================================================================
 * Build (write) path
 * ========================================================================== */

static YamlSequence *
build_vector3_sequence (GrlVector3 *v)
{
	YamlSequence *seq = yaml_sequence_new ();

	yaml_sequence_add_double_element (seq, (gdouble) (v != NULL ? v->x : 0.0f));
	yaml_sequence_add_double_element (seq, (gdouble) (v != NULL ? v->y : 0.0f));
	yaml_sequence_add_double_element (seq, (gdouble) (v != NULL ? v->z : 0.0f));

	return seq;
}

static YamlMapping *
build_material_mapping (LrgMaterial3D *material)
{
	YamlMapping             *map = yaml_mapping_new ();
	g_autoptr(YamlSequence)  color_seq = yaml_sequence_new ();
	gfloat                   r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

	lrg_material3d_get_color (material, &r, &g, &b, &a);
	yaml_sequence_add_double_element (color_seq, (gdouble) r);
	yaml_sequence_add_double_element (color_seq, (gdouble) g);
	yaml_sequence_add_double_element (color_seq, (gdouble) b);
	yaml_sequence_add_double_element (color_seq, (gdouble) a);
	yaml_mapping_set_sequence_member (map, "color", color_seq);

	yaml_mapping_set_double_member (map, "roughness",
	                                (gdouble) lrg_material3d_get_roughness (material));
	yaml_mapping_set_double_member (map, "metallic",
	                                (gdouble) lrg_material3d_get_metallic (material));

	/* Emission only when it glows, so older levels stay unchanged. */
	if (lrg_material3d_get_emission_strength (material) > 0.0f)
	{
		g_autoptr(YamlSequence) emission_seq = yaml_sequence_new ();
		gfloat                  er = 0.0f, eg = 0.0f, eb = 0.0f;

		lrg_material3d_get_emission_color (material, &er, &eg, &eb, NULL);
		yaml_sequence_add_double_element (emission_seq, (gdouble) er);
		yaml_sequence_add_double_element (emission_seq, (gdouble) eg);
		yaml_sequence_add_double_element (emission_seq, (gdouble) eb);
		yaml_mapping_set_sequence_member (map, "emission", emission_seq);
		yaml_mapping_set_double_member (map, "emission_strength",
		                                (gdouble) lrg_material3d_get_emission_strength (material));
	}

	return map;
}

/*
 * Tile grid of a tilemap visual. Each layer's cells are written as one
 * string per row of space-separated tile ids, which keeps .rlevel diffs
 * readable and line-oriented.
 */
static YamlMapping *
build_tilemap_mapping (LrgNodeTilemap *tilemap)
{
	YamlMapping             *map = yaml_mapping_new ();
	g_autoptr(YamlSequence)  layers = yaml_sequence_new ();
	guint                    w = lrg_node_tilemap_get_width (tilemap);
	guint                    h = lrg_node_tilemap_get_height (tilemap);
	guint                    i, x, y;

	yaml_mapping_set_int_member (map, "width", w);
	yaml_mapping_set_int_member (map, "height", h);
	yaml_mapping_set_int_member (map, "tile_width", lrg_node_tilemap_get_tile_width (tilemap));
	yaml_mapping_set_int_member (map, "tile_height", lrg_node_tilemap_get_tile_height (tilemap));

	for (i = 0; i < lrg_node_tilemap_get_n_layers (tilemap); i++)
	{
		g_autoptr(YamlMapping)  lmap = yaml_mapping_new ();
		g_autoptr(YamlSequence) rows = yaml_sequence_new ();
		gsize                   n = 0;
		const guint            *tiles = lrg_node_tilemap_get_layer_tiles (tilemap, i, &n);

		yaml_mapping_set_string_member (lmap, "name", lrg_node_tilemap_get_layer_name (tilemap, i));
		yaml_mapping_set_boolean_member (lmap, "visible", lrg_node_tilemap_get_layer_visible (tilemap, i));
		for (y = 0; tiles != NULL && y < h; y++)
		{
			g_autoptr(GString) row = g_string_sized_new (w * 2);

			for (x = 0; x < w; x++)
				g_string_append_printf (row, x == 0 ? "%u" : " %u", tiles[(gsize) y * w + x]);
			yaml_sequence_add_string_element (rows, row->str);
		}
		yaml_mapping_set_sequence_member (lmap, "rows", rows);
		yaml_sequence_add_mapping_element (layers, lmap);
	}
	yaml_mapping_set_sequence_member (map, "layers", layers);
	return map;
}

static YamlMapping *
build_visual_mapping (LrgNodeVisual *visual)
{
	YamlMapping  *map = yaml_mapping_new ();
	const gchar  *kind_nick;
	const gchar  *asset;
	LrgMaterial3D *material;
	GList        *param_names;
	GList        *iter;

	kind_nick = enum_to_nick (LRG_TYPE_NODE_VISUAL_KIND,
	                          lrg_node_visual_get_kind (visual));
	if (kind_nick != NULL)
		yaml_mapping_set_string_member (map, "kind", kind_nick);

	if (lrg_node_visual_get_kind (visual) == LRG_NODE_VISUAL_PRIMITIVE)
	{
		const gchar *prim_nick = enum_to_nick (LRG_TYPE_PRIMITIVE_TYPE,
		                                        lrg_node_visual_get_primitive (visual));
		if (prim_nick != NULL)
			yaml_mapping_set_string_member (map, "primitive", prim_nick);
	}

	asset = lrg_node_visual_get_asset (visual);
	if (asset != NULL)
		yaml_mapping_set_string_member (map, "asset", asset);

	/* A material asset reference (`material: guid:...` or a .rmat path)
	 * wins over the inline block, which then stays in memory only. */
	material = lrg_node_visual_get_material (visual);
	if (lrg_node_visual_get_material_ref (visual) != NULL)
		yaml_mapping_set_string_member (map, "material", lrg_node_visual_get_material_ref (visual));
	else if (material != NULL)
	{
		g_autoptr(YamlMapping) mat_map = build_material_mapping (material);
		yaml_mapping_set_mapping_member (map, "material", mat_map);
	}

	param_names = lrg_node_visual_get_param_names (visual);
	if (param_names != NULL)
	{
		g_autoptr(YamlMapping) params_map = yaml_mapping_new ();

		for (iter = param_names; iter != NULL; iter = iter->next)
		{
			const gchar  *name = iter->data;
			const GValue *value = lrg_node_visual_get_param (visual, name);

			if (value != NULL)
				set_value_member (params_map, name, value);
		}
		yaml_mapping_set_mapping_member (map, "params", params_map);
	}
	g_list_free (param_names);

	if (lrg_node_visual_get_tilemap (visual) != NULL)
	{
		g_autoptr(YamlMapping) tmap = build_tilemap_mapping (lrg_node_visual_get_tilemap (visual));

		yaml_mapping_set_mapping_member (map, "tilemap", tmap);
	}

	return map;
}

static YamlMapping *
build_component_mapping (LrgComponentDesc *desc)
{
	YamlMapping *map = yaml_mapping_new ();
	const gchar *type_name;
	GList       *keys;
	GList       *iter;

	type_name = lrg_component_desc_get_type_name (desc);
	if (type_name != NULL)
		yaml_mapping_set_string_member (map, "type", type_name);

	keys = lrg_component_desc_get_keys (desc);
	if (keys != NULL)
	{
		g_autoptr(YamlMapping) props_map = yaml_mapping_new ();

		for (iter = keys; iter != NULL; iter = iter->next)
		{
			const gchar  *name = iter->data;
			const GValue *value = lrg_component_desc_get_value (desc, name);

			if (value != NULL)
				set_value_member (props_map, name, value);
		}
		yaml_mapping_set_mapping_member (map, "props", props_map);
	}
	g_list_free (keys);

	return map;
}

static YamlMapping *
build_script_mapping (LrgScriptBinding *binding)
{
	YamlMapping *map = yaml_mapping_new ();
	const gchar *lang_nick;
	const gchar *script;

	lang_nick = enum_to_nick (LRG_TYPE_SCRIPT_LANGUAGE,
	                          lrg_script_binding_get_language (binding));
	if (lang_nick != NULL)
		yaml_mapping_set_string_member (map, "language", lang_nick);

	script = lrg_script_binding_get_script (binding);
	if (script != NULL)
		yaml_mapping_set_string_member (map, "script", script);

	yaml_mapping_set_boolean_member (map, "enabled",
	                                 lrg_script_binding_get_enabled (binding));

	return map;
}

/*
 * build_prefab_mapping:
 * @link: the link to write
 * @full: (nullable): for a reference-stored instance, the instance root
 * @sparse: (nullable): for a reference-stored instance, its sparse tree
 *
 * The optional `prefab:` block of a linked instance root:
 *
 *   prefab:
 *     source: prefabs/crate.rprefab
 *     guid: <asset guid>            (omitted when unknown)
 *     storage: reference            (0.9.0; omitted for expanded storage)
 *     map: { <instance guid>: <source guid>, ... }
 *     overrides: [ <key>, ... ]     (omitted when empty)
 *
 * In reference storage the map only lists the entries a loader cannot
 * re-derive: guids that differ from lrg_prefab_derive_guid() of the root
 * guid, every node the sparse tree records (so a loader can tell a
 * recorded mapped node from an instance-added one) and entries of nodes
 * the instance no longer has.
 */
static YamlMapping *
build_prefab_mapping (LrgPrefabLink *link,
                      LrgNode       *full,
                      LrgNode       *sparse)
{
	const gchar            *root_guid = (full != NULL) ? lrg_node_get_guid (full) : NULL;
	YamlMapping            *map = yaml_mapping_new ();
	g_autoptr(YamlMapping)  guid_map = yaml_mapping_new ();
	g_auto(GStrv)           guids = lrg_prefab_link_get_mapped_guids (link);
	g_auto(GStrv)           keys = lrg_prefab_link_get_overrides (link);
	guint                   i;

	if (lrg_prefab_link_get_source_path (link) != NULL)
		yaml_mapping_set_string_member (map, "source", lrg_prefab_link_get_source_path (link));
	if (lrg_prefab_link_get_source_guid (link) != NULL)
		yaml_mapping_set_string_member (map, "guid", lrg_prefab_link_get_source_guid (link));
	if (sparse != NULL)
		yaml_mapping_set_string_member (map, "storage", "reference");

	for (i = 0; guids != NULL && guids[i] != NULL; i++)
	{
		const gchar *sg = lrg_prefab_link_map_lookup (link, guids[i]);

		/* Entries of nodes the instance no longer has are kept too. */
		if (sparse != NULL && root_guid != NULL && lrg_node_find_by_guid (sparse, guids[i]) == NULL &&
		    lrg_node_find_by_guid (full, guids[i]) != NULL)
		{
			g_autofree gchar *derived = lrg_prefab_derive_guid (root_guid, sg);

			if (g_strcmp0 (derived, guids[i]) == 0)
				continue;  /* re-derived on load */
		}
		yaml_mapping_set_string_member (guid_map, guids[i], sg);
	}
	yaml_mapping_set_mapping_member (map, "map", guid_map);

	if (keys != NULL && keys[0] != NULL)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();

		for (i = 0; keys[i] != NULL; i++)
			yaml_sequence_add_string_element (seq, keys[i]);
		yaml_mapping_set_sequence_member (map, "overrides", seq);
	}

	return map;
}

/* ==========================================================================
 * The node `physics:` block (0.9.0 M5)
 *
 * physics:
 *   body: dynamic                  # static | kinematic | dynamic
 *   mass: 2                        # kg (dynamic bodies)
 *   layer: debris                  # omitted for the default layer
 *   friction: 0.5
 *   restitution: 0
 *   linear_damping: 0.05
 *   angular_damping: 0.05
 *   gravity_factor: 1
 *   ccd: true                      # omitted when off
 *   shapes:
 *     - kind: box                  # sphere|capsule|cylinder|box|convex|mesh|plane
 *       half_extents: [0.5, 0.5, 0.5]   # box only
 *       radius: 0.5                # sphere / capsule / cylinder
 *       half_height: 0.5           # capsule / cylinder
 *       mesh: models/rock.glb      # convex / mesh; omitted = the node's model
 *       offset: [0, 0.5, 0]        # omitted when zero
 *       rotation: [0, 0, 90]       # degrees; omitted when zero
 *       trigger: true              # omitted when false
 *       friction: 0.9              # omitted = the body's
 *       restitution: 0.5           # omitted = the body's
 *       layer: triggers            # omitted = the body's
 *   joints:
 *     - kind: hinge                # fixed|point|hinge|slider|distance|cone|swing-twist|six-dof
 *       target: <node guid>        # omitted = the world
 *       anchor: [0, 1, 0]          # node-local; omitted when zero
 *       axis: [0, 0, 1]            # node-local; omitted when +Y
 *       limits: [-1.57, 1.57]      # omitted when off
 *       spring: [4, 0.7]           # Hz, damping; omitted when 0 Hz
 *       motor: [3.14, 50]          # velocity, max force; omitted when off
 *       break_force: 500           # omitted when 0
 *
 * Every key is written in this order and only when it differs from its
 * default where noted, so a load / save round trip is byte stable.
 * ========================================================================== */

static YamlSequence *
build_float3_sequence (const gfloat *v)
{
	YamlSequence *seq = yaml_sequence_new ();
	guint         k;

	for (k = 0; k < 3; k++)
		yaml_sequence_add_double_element (seq, (gdouble) v[k]);
	return seq;
}

static YamlSequence *
build_float2_sequence (gfloat a,
                       gfloat b)
{
	YamlSequence *seq = yaml_sequence_new ();

	yaml_sequence_add_double_element (seq, (gdouble) a);
	yaml_sequence_add_double_element (seq, (gdouble) b);
	return seq;
}

static gboolean
float3_is (const gfloat *v,
           gfloat        x,
           gfloat        y,
           gfloat        z)
{
	return v[0] == x && v[1] == y && v[2] == z;
}

static void
set_float3_member (YamlMapping  *map,
                   const gchar  *key,
                   const gfloat *v)
{
	g_autoptr(YamlSequence) seq = build_float3_sequence (v);

	yaml_mapping_set_sequence_member (map, key, seq);
}

static YamlMapping *
build_physics_shape_mapping (LrgNodePhysicsShape *shape)
{
	YamlMapping       *map = yaml_mapping_new ();
	LrgCollider3DKind  kind = lrg_node_physics_shape_get_kind (shape);
	gfloat             v[3];

	yaml_mapping_set_string_member (map, "kind", enum_to_nick (LRG_TYPE_COLLIDER3D_KIND, kind));
	switch (kind)
	{
	case LRG_COLLIDER3D_BOX:
		lrg_node_physics_shape_get_half_extents (shape, v);
		set_float3_member (map, "half_extents", v);
		break;
	case LRG_COLLIDER3D_SPHERE:
		yaml_mapping_set_double_member (map, "radius", (gdouble) lrg_node_physics_shape_get_radius (shape));
		break;
	case LRG_COLLIDER3D_CAPSULE:
	case LRG_COLLIDER3D_CYLINDER:
		yaml_mapping_set_double_member (map, "radius", (gdouble) lrg_node_physics_shape_get_radius (shape));
		yaml_mapping_set_double_member (map, "half_height",
		                                (gdouble) lrg_node_physics_shape_get_half_height (shape));
		break;
	case LRG_COLLIDER3D_CONVEX:
	case LRG_COLLIDER3D_MESH:
		if (lrg_node_physics_shape_get_mesh (shape) != NULL)
			yaml_mapping_set_string_member (map, "mesh", lrg_node_physics_shape_get_mesh (shape));
		break;
	case LRG_COLLIDER3D_PLANE:
	default:
		break;
	}

	lrg_node_physics_shape_get_offset (shape, v);
	if (!float3_is (v, 0.0f, 0.0f, 0.0f))
		set_float3_member (map, "offset", v);
	lrg_node_physics_shape_get_rotation (shape, v);
	if (!float3_is (v, 0.0f, 0.0f, 0.0f))
		set_float3_member (map, "rotation", v);
	if (lrg_node_physics_shape_get_trigger (shape))
		yaml_mapping_set_boolean_member (map, "trigger", TRUE);
	if (lrg_node_physics_shape_get_friction (shape) >= 0.0f)
		yaml_mapping_set_double_member (map, "friction", (gdouble) lrg_node_physics_shape_get_friction (shape));
	if (lrg_node_physics_shape_get_restitution (shape) >= 0.0f)
		yaml_mapping_set_double_member (map, "restitution",
		                                (gdouble) lrg_node_physics_shape_get_restitution (shape));
	if (lrg_node_physics_shape_get_layer (shape) != NULL)
		yaml_mapping_set_string_member (map, "layer", lrg_node_physics_shape_get_layer (shape));
	return map;
}

static YamlMapping *
build_physics_joint_mapping (LrgNodePhysicsJoint *joint)
{
	YamlMapping *map = yaml_mapping_new ();
	gfloat       v[3];
	gfloat       a;
	gfloat       b;

	yaml_mapping_set_string_member (map, "kind", enum_to_nick (LRG_TYPE_JOINT_KIND,
	                                                           lrg_node_physics_joint_get_kind (joint)));
	if (lrg_node_physics_joint_get_target (joint) != NULL)
		yaml_mapping_set_string_member (map, "target", lrg_node_physics_joint_get_target (joint));
	lrg_node_physics_joint_get_anchor (joint, v);
	if (!float3_is (v, 0.0f, 0.0f, 0.0f))
		set_float3_member (map, "anchor", v);
	lrg_node_physics_joint_get_axis (joint, v);
	if (!float3_is (v, 0.0f, 1.0f, 0.0f))
		set_float3_member (map, "axis", v);
	if (lrg_node_physics_joint_get_limits (joint, &a, &b))
	{
		g_autoptr(YamlSequence) seq = build_float2_sequence (a, b);

		yaml_mapping_set_sequence_member (map, "limits", seq);
	}
	lrg_node_physics_joint_get_spring (joint, &a, &b);
	if (a > 0.0f)
	{
		g_autoptr(YamlSequence) seq = build_float2_sequence (a, b);

		yaml_mapping_set_sequence_member (map, "spring", seq);
	}
	lrg_node_physics_joint_get_motor (joint, &a, &b);
	if (b > 0.0f)
	{
		g_autoptr(YamlSequence) seq = build_float2_sequence (a, b);

		yaml_mapping_set_sequence_member (map, "motor", seq);
	}
	if (lrg_node_physics_joint_get_break_force (joint) > 0.0f)
		yaml_mapping_set_double_member (map, "break_force",
		                                (gdouble) lrg_node_physics_joint_get_break_force (joint));
	return map;
}

static YamlMapping *
build_physics_mapping (LrgNodePhysics *physics)
{
	YamlMapping *map = yaml_mapping_new ();
	GPtrArray   *shapes = lrg_node_physics_get_shapes (physics);
	GPtrArray   *joints = lrg_node_physics_get_joints (physics);
	gfloat       lin;
	gfloat       ang;
	guint        i;

	yaml_mapping_set_string_member (map, "body", enum_to_nick (LRG_TYPE_DYNAMICS_BODY_KIND,
	                                                           lrg_node_physics_get_body_kind (physics)));
	yaml_mapping_set_double_member (map, "mass", (gdouble) lrg_node_physics_get_mass (physics));
	if (lrg_node_physics_get_layer (physics) != NULL)
		yaml_mapping_set_string_member (map, "layer", lrg_node_physics_get_layer (physics));
	yaml_mapping_set_double_member (map, "friction", (gdouble) lrg_node_physics_get_friction (physics));
	yaml_mapping_set_double_member (map, "restitution", (gdouble) lrg_node_physics_get_restitution (physics));
	lrg_node_physics_get_damping (physics, &lin, &ang);
	yaml_mapping_set_double_member (map, "linear_damping", (gdouble) lin);
	yaml_mapping_set_double_member (map, "angular_damping", (gdouble) ang);
	yaml_mapping_set_double_member (map, "gravity_factor", (gdouble) lrg_node_physics_get_gravity_factor (physics));
	if (lrg_node_physics_get_ccd (physics))
		yaml_mapping_set_boolean_member (map, "ccd", TRUE);

	if (shapes->len > 0)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();

		for (i = 0; i < shapes->len; i++)
		{
			g_autoptr(YamlMapping) m = build_physics_shape_mapping (g_ptr_array_index (shapes, i));

			yaml_sequence_add_mapping_element (seq, m);
		}
		yaml_mapping_set_sequence_member (map, "shapes", seq);
	}
	if (joints->len > 0)
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();

		for (i = 0; i < joints->len; i++)
		{
			g_autoptr(YamlMapping) m = build_physics_joint_mapping (g_ptr_array_index (joints, i));

			yaml_sequence_add_mapping_element (seq, m);
		}
		yaml_mapping_set_sequence_member (map, "joints", seq);
	}
	return map;
}

/* A number member, or @def when absent. */
static gfloat
phys_float (YamlMapping *map,
            const gchar *key,
            gfloat       def)
{
	return yaml_mapping_has_member (map, key) ? (gfloat) yaml_mapping_get_double_member (map, key) : def;
}

/* A sequence member of 2 or 3 numbers, or NULL. */
static YamlSequence *
phys_seq (YamlMapping *map,
          const gchar *key)
{
	YamlNode *node = yaml_mapping_get_member (map, key);

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return NULL;
	return yaml_node_get_sequence (node);
}

static LrgNodePhysicsShape *
parse_physics_shape (YamlMapping *map)
{
	LrgNodePhysicsShape *shape;
	YamlSequence        *seq;

	shape = lrg_node_physics_shape_new (nick_to_enum (LRG_TYPE_COLLIDER3D_KIND,
	                                                  yaml_mapping_get_string_member (map, "kind"),
	                                                  LRG_COLLIDER3D_BOX));
	if ((seq = phys_seq (map, "half_extents")) != NULL)
		lrg_node_physics_shape_set_half_extents (shape, (gfloat) seq_get_double (seq, 0, 0.5),
		                                         (gfloat) seq_get_double (seq, 1, 0.5),
		                                         (gfloat) seq_get_double (seq, 2, 0.5));
	lrg_node_physics_shape_set_radius (shape, phys_float (map, "radius", 0.5f));
	lrg_node_physics_shape_set_half_height (shape, phys_float (map, "half_height", 0.5f));
	if (yaml_mapping_has_member (map, "mesh"))
		lrg_node_physics_shape_set_mesh (shape, yaml_mapping_get_string_member (map, "mesh"));
	if ((seq = phys_seq (map, "offset")) != NULL)
		lrg_node_physics_shape_set_offset (shape, (gfloat) seq_get_double (seq, 0, 0.0),
		                                   (gfloat) seq_get_double (seq, 1, 0.0),
		                                   (gfloat) seq_get_double (seq, 2, 0.0));
	if ((seq = phys_seq (map, "rotation")) != NULL)
		lrg_node_physics_shape_set_rotation (shape, (gfloat) seq_get_double (seq, 0, 0.0),
		                                     (gfloat) seq_get_double (seq, 1, 0.0),
		                                     (gfloat) seq_get_double (seq, 2, 0.0));
	if (yaml_mapping_has_member (map, "trigger"))
		lrg_node_physics_shape_set_trigger (shape, yaml_mapping_get_boolean_member (map, "trigger"));
	lrg_node_physics_shape_set_friction (shape, phys_float (map, "friction", -1.0f));
	lrg_node_physics_shape_set_restitution (shape, phys_float (map, "restitution", -1.0f));
	if (yaml_mapping_has_member (map, "layer"))
		lrg_node_physics_shape_set_layer (shape, yaml_mapping_get_string_member (map, "layer"));
	return shape;
}

static LrgNodePhysicsJoint *
parse_physics_joint (YamlMapping *map)
{
	LrgNodePhysicsJoint *joint;
	YamlSequence        *seq;

	joint = lrg_node_physics_joint_new (nick_to_enum (LRG_TYPE_JOINT_KIND,
	                                                  yaml_mapping_get_string_member (map, "kind"),
	                                                  LRG_JOINT_POINT));
	if (yaml_mapping_has_member (map, "target"))
		lrg_node_physics_joint_set_target (joint, yaml_mapping_get_string_member (map, "target"));
	if ((seq = phys_seq (map, "anchor")) != NULL)
		lrg_node_physics_joint_set_anchor (joint, (gfloat) seq_get_double (seq, 0, 0.0),
		                                   (gfloat) seq_get_double (seq, 1, 0.0),
		                                   (gfloat) seq_get_double (seq, 2, 0.0));
	if ((seq = phys_seq (map, "axis")) != NULL)
		lrg_node_physics_joint_set_axis (joint, (gfloat) seq_get_double (seq, 0, 0.0),
		                                 (gfloat) seq_get_double (seq, 1, 1.0),
		                                 (gfloat) seq_get_double (seq, 2, 0.0));
	if ((seq = phys_seq (map, "limits")) != NULL)
		lrg_node_physics_joint_set_limits (joint, TRUE, (gfloat) seq_get_double (seq, 0, 0.0),
		                                   (gfloat) seq_get_double (seq, 1, 0.0));
	if ((seq = phys_seq (map, "spring")) != NULL)
		lrg_node_physics_joint_set_spring (joint, (gfloat) seq_get_double (seq, 0, 0.0),
		                                   (gfloat) seq_get_double (seq, 1, 1.0));
	if ((seq = phys_seq (map, "motor")) != NULL)
		lrg_node_physics_joint_set_motor (joint, (gfloat) seq_get_double (seq, 0, 0.0),
		                                  (gfloat) seq_get_double (seq, 1, 0.0));
	lrg_node_physics_joint_set_break_force (joint, phys_float (map, "break_force", 0.0f));
	return joint;
}

static LrgNodePhysics *
parse_physics (YamlMapping *map)
{
	LrgNodePhysics *physics;
	YamlSequence   *seq;
	guint           i;

	physics = lrg_node_physics_new (nick_to_enum (LRG_TYPE_DYNAMICS_BODY_KIND,
	                                              yaml_mapping_get_string_member (map, "body"),
	                                              LRG_DYNAMICS_BODY_STATIC));
	lrg_node_physics_set_mass (physics, phys_float (map, "mass", 1.0f));
	if (yaml_mapping_has_member (map, "layer"))
		lrg_node_physics_set_layer (physics, yaml_mapping_get_string_member (map, "layer"));
	lrg_node_physics_set_friction (physics, phys_float (map, "friction", 0.5f));
	lrg_node_physics_set_restitution (physics, phys_float (map, "restitution", 0.0f));
	lrg_node_physics_set_damping (physics, phys_float (map, "linear_damping", 0.05f),
	                              phys_float (map, "angular_damping", 0.05f));
	lrg_node_physics_set_gravity_factor (physics, phys_float (map, "gravity_factor", 1.0f));
	if (yaml_mapping_has_member (map, "ccd"))
		lrg_node_physics_set_ccd (physics, yaml_mapping_get_boolean_member (map, "ccd"));

	if ((seq = phys_seq (map, "shapes")) != NULL)
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			YamlNode *n = yaml_sequence_get_element (seq, i);

			if (n != NULL && yaml_node_get_node_type (n) == YAML_NODE_MAPPING)
			{
				g_autoptr(LrgNodePhysicsShape) shape = parse_physics_shape (yaml_node_get_mapping (n));

				lrg_node_physics_add_shape (physics, shape);
			}
		}
	if ((seq = phys_seq (map, "joints")) != NULL)
		for (i = 0; i < yaml_sequence_get_length (seq); i++)
		{
			YamlNode *n = yaml_sequence_get_element (seq, i);

			if (n != NULL && yaml_node_get_node_type (n) == YAML_NODE_MAPPING)
			{
				g_autoptr(LrgNodePhysicsJoint) joint = parse_physics_joint (yaml_node_get_mapping (n));

				lrg_node_physics_add_joint (physics, joint);
			}
		}
	return physics;
}

/*
 * WriteRef: while writing the sparse tree of a reference-stored instance,
 * the instance's (full) link, used to tell recorded mapped nodes from
 * instance-added ones.
 */
typedef struct
{
	LrgPrefabLink *link;
} WriteRef;

static YamlMapping *build_node_mapping (LrgNode *node, LrgPrefabStorage storage, WriteRef *ref);

/* TRUE when the link has an override key that names @guid. */
static gboolean
link_names_guid (LrgPrefabLink *link,
                 const gchar   *guid)
{
	g_auto(GStrv)     keys = lrg_prefab_link_get_overrides (link);
	g_autofree gchar *qual = g_strconcat (guid, ":", NULL);
	g_autofree gchar *added = g_strconcat ("children/+", guid, NULL);
	guint             i;

	for (i = 0; keys != NULL && keys[i] != NULL; i++)
		if (g_str_has_prefix (keys[i], qual) || g_str_has_suffix (keys[i], added))
			return TRUE;
	return FALSE;
}

/* Writes the node's own state (everything but the link and children). */
static void
build_node_state (LrgNode     *node,
                  YamlMapping *map)
{
	g_autoptr(YamlMapping)   transform_map = yaml_mapping_new ();
	g_autoptr(YamlSequence)  loc_seq = NULL;
	g_autoptr(YamlSequence)  rot_seq = NULL;
	g_autoptr(YamlSequence)  scl_seq = NULL;
	LrgNodeVisual           *visual;
	GPtrArray               *components;
	GPtrArray               *scripts;
	guint                    i;

	if (lrg_node_get_name (node) != NULL)
		yaml_mapping_set_string_member (map, "name", lrg_node_get_name (node));
	if (lrg_node_get_guid (node) != NULL)
		yaml_mapping_set_string_member (map, "guid", lrg_node_get_guid (node));

	yaml_mapping_set_boolean_member (map, "is_2d", lrg_node_get_is_2d (node));
	yaml_mapping_set_boolean_member (map, "visible", lrg_node_get_visible (node));
	yaml_mapping_set_boolean_member (map, "locked", lrg_node_get_locked (node));

	loc_seq = build_vector3_sequence (lrg_node_get_location (node));
	rot_seq = build_vector3_sequence (lrg_node_get_rotation (node));
	scl_seq = build_vector3_sequence (lrg_node_get_scale (node));
	yaml_mapping_set_sequence_member (transform_map, "location", loc_seq);
	yaml_mapping_set_sequence_member (transform_map, "rotation", rot_seq);
	yaml_mapping_set_sequence_member (transform_map, "scale", scl_seq);
	yaml_mapping_set_mapping_member (map, "transform", transform_map);

	visual = lrg_node_get_visual (node);
	if (visual != NULL)
	{
		g_autoptr(YamlMapping) visual_map = build_visual_mapping (visual);
		yaml_mapping_set_mapping_member (map, "visual", visual_map);
	}

	if (lrg_node_get_physics (node) != NULL)
	{
		g_autoptr(YamlMapping) physics_map = build_physics_mapping (lrg_node_get_physics (node));

		yaml_mapping_set_mapping_member (map, "physics", physics_map);
	}

	components = lrg_node_get_components (node);
	if (components != NULL && components->len > 0)
	{
		g_autoptr(YamlSequence) comp_seq = yaml_sequence_new ();

		for (i = 0; i < components->len; i++)
		{
			LrgComponentDesc       *desc = g_ptr_array_index (components, i);
			g_autoptr(YamlMapping)  comp_map = build_component_mapping (desc);

			yaml_sequence_add_mapping_element (comp_seq, comp_map);
		}
		yaml_mapping_set_sequence_member (map, "components", comp_seq);
	}

	scripts = lrg_node_get_scripts (node);
	if (scripts != NULL && scripts->len > 0)
	{
		g_autoptr(YamlSequence) script_seq = yaml_sequence_new ();

		for (i = 0; i < scripts->len; i++)
		{
			LrgScriptBinding       *binding = g_ptr_array_index (scripts, i);
			g_autoptr(YamlMapping)  script_map = build_script_mapping (binding);

			yaml_sequence_add_mapping_element (script_seq, script_map);
		}
		yaml_mapping_set_sequence_member (map, "scripts", script_seq);
	}
}

/* Writes the children of @node (if any) as `children:`. */
static void
build_children (LrgNode          *node,
                YamlMapping      *map,
                LrgPrefabStorage  storage,
                WriteRef         *ref)
{
	GPtrArray               *children = lrg_node_get_children (node);
	g_autoptr(YamlSequence)  child_seq = NULL;
	guint                    i;

	if (children == NULL || children->len == 0)
		return;

	child_seq = yaml_sequence_new ();
	for (i = 0; i < children->len; i++)
	{
		g_autoptr(YamlMapping) child_map = build_node_mapping (g_ptr_array_index (children, i), storage, ref);

		yaml_sequence_add_mapping_element (child_seq, child_map);
	}
	yaml_mapping_set_sequence_member (map, "children", child_seq);
}

/*
 * build_node_mapping:
 * @node: the node to write
 * @storage: how linked instance roots are stored
 * @ref: (nullable): set while writing inside a reference-stored instance
 *
 * Writes one node and its children. A linked instance root in reference
 * storage is written as its sparse form (lrg_prefab_make_reference());
 * inside it, recorded mapped nodes without overrides or children shrink
 * to placeholders (`name` and `guid`), mapped nodes drop their nested
 * links (the source brings them back), and instance-added subtrees are
 * written normally (their own linked roots may be references again).
 */
static YamlMapping *
build_node_mapping (LrgNode          *node,
                    LrgPrefabStorage  storage,
                    WriteRef         *ref)
{
	YamlMapping *map = yaml_mapping_new ();
	gboolean     mapped = FALSE;

	/* 1. Inside a reference: an instance-added node is written like any
	 *    other node; a recorded mapped node without overrides or children
	 *    is a placeholder. */
	if (ref != NULL)
	{
		mapped = lrg_prefab_link_map_lookup (ref->link, lrg_node_get_guid (node)) != NULL;
		if (!mapped)
			ref = NULL;
		else if (lrg_node_get_children (node)->len == 0 &&
		         !link_names_guid (ref->link, lrg_node_get_guid (node)))
		{
			if (lrg_node_get_name (node) != NULL)
				yaml_mapping_set_string_member (map, "name", lrg_node_get_name (node));
			yaml_mapping_set_string_member (map, "guid", lrg_node_get_guid (node));
			return map;
		}
	}

	/* 2. A linked instance root (outside any reference) in reference
	 *    storage: its sparse form. */
	if (ref == NULL && storage == LRG_PREFAB_STORAGE_REFERENCE &&
	    lrg_node_get_prefab_link (node) != NULL)
	{
		g_autoptr(LrgNode)      sparse = lrg_prefab_make_reference (node);
		g_autoptr(YamlMapping)  prefab_map = NULL;
		WriteRef                inner;

		inner.link = lrg_node_get_prefab_link (node);
		build_node_state (sparse, map);
		prefab_map = build_prefab_mapping (inner.link, node, sparse);
		yaml_mapping_set_mapping_member (map, "prefab", prefab_map);
		build_children (sparse, map, storage, &inner);
		return map;
	}

	/* 3. A plain node (or a recorded mapped node of a reference, which
	 *    drops its nested link: the source brings it back). */
	build_node_state (node, map);
	if (lrg_node_get_prefab_link (node) != NULL && !mapped)
	{
		g_autoptr(YamlMapping) prefab_map = build_prefab_mapping (lrg_node_get_prefab_link (node), NULL, NULL);

		yaml_mapping_set_mapping_member (map, "prefab", prefab_map);
	}
	build_children (node, map, storage, ref);
	return map;
}

/*
 * Level timelines. Keys are written as small mappings
 * ({time, value, easing}); easing uses the #LrgEasingType nick.
 */
static YamlSequence *
build_timelines_sequence (LrgLevel *level)
{
	YamlSequence *seq = yaml_sequence_new ();
	guint         i, j, k;

	for (i = 0; i < lrg_level_get_n_timelines (level); i++)
	{
		LrgTimeline             *tl = lrg_level_get_timeline (level, i);
		g_autoptr(YamlMapping)   map = yaml_mapping_new ();
		g_autoptr(YamlSequence)  tracks = yaml_sequence_new ();

		yaml_mapping_set_string_member (map, "name", lrg_timeline_get_name (tl));
		yaml_mapping_set_double_member (map, "length", lrg_timeline_get_length (tl));
		yaml_mapping_set_boolean_member (map, "loop", lrg_timeline_get_loop (tl));
		yaml_mapping_set_boolean_member (map, "autoplay", lrg_timeline_get_autoplay (tl));

		for (j = 0; j < lrg_timeline_get_n_tracks (tl); j++)
		{
			LrgTimelineTrack        *track = lrg_timeline_get_track (tl, j);
			g_autoptr(YamlMapping)   tmap = yaml_mapping_new ();
			g_autoptr(YamlSequence)  keys = yaml_sequence_new ();

			yaml_mapping_set_string_member (tmap, "node", lrg_timeline_track_get_node_guid (track));
			yaml_mapping_set_string_member (tmap, "channel", lrg_timeline_track_get_channel (track));
			for (k = 0; k < lrg_timeline_track_get_n_keys (track); k++)
			{
				g_autoptr(YamlMapping) kmap = yaml_mapping_new ();
				gfloat                 t = 0.0f, v = 0.0f;
				LrgEasingType          e = LRG_EASING_LINEAR;
				const gchar           *nick;
				LrgSequenceTangentMode tmode = LRG_SEQUENCE_TANGENT_AUTO;
				gfloat                 tin = 0.0f, tout = 0.0f;

				lrg_timeline_track_get_key (track, k, &t, &v, &e);
				yaml_mapping_set_double_member (kmap, "time", t);
				yaml_mapping_set_double_member (kmap, "value", v);
				nick = enum_to_nick (LRG_TYPE_EASING_TYPE, e);
				if (nick != NULL && e != LRG_EASING_LINEAR)
					yaml_mapping_set_string_member (kmap, "easing", nick);
				/* Bézier keys (0.9.0): tangent mode nick, user slope(s) */
				if (lrg_timeline_track_get_key_tangents (track, k, &tmode, &tin, &tout))
				{
					yaml_mapping_set_string_member (kmap, "tangent",
					                                enum_to_nick (LRG_TYPE_SEQUENCE_TANGENT_MODE, (gint) tmode));
					if (tmode == LRG_SEQUENCE_TANGENT_USER)
						yaml_mapping_set_double_member (kmap, "slope", tin);
					else if (tmode == LRG_SEQUENCE_TANGENT_BROKEN)
					{
						yaml_mapping_set_double_member (kmap, "in", tin);
						yaml_mapping_set_double_member (kmap, "out", tout);
					}
				}
				yaml_sequence_add_mapping_element (keys, kmap);
			}
			yaml_mapping_set_sequence_member (tmap, "keys", keys);
			yaml_sequence_add_mapping_element (tracks, tmap);
		}
		yaml_mapping_set_sequence_member (map, "tracks", tracks);
		yaml_sequence_add_mapping_element (seq, map);
	}
	return seq;
}

static YamlNode *
build_level_yaml (LrgLevel         *level,
                  LrgPrefabStorage  storage)
{
	g_autoptr(YamlMapping)   root_map = yaml_mapping_new ();
	g_autoptr(YamlSequence)  nodes_seq = yaml_sequence_new ();
	LrgNode                 *root;
	GPtrArray               *top;
	guint                    i;

	yaml_mapping_set_string_member (root_map, "type", "level");
	if (lrg_level_get_name (level) != NULL)
		yaml_mapping_set_string_member (root_map, "name", lrg_level_get_name (level));
	yaml_mapping_set_string_member (root_map, "default_view",
	                                lrg_level_get_default_2d (level) ? "2d" : "3d");
	/* The level's own storage setting (0.9.0), only when it has one. */
	if (lrg_prefab_level_get_storage (level) != LRG_PREFAB_STORAGE_DEFAULT)
		yaml_mapping_set_string_member (root_map, STORAGE_KEY,
		                                lrg_prefab_storage_to_string (lrg_prefab_level_get_storage (level)));

	root = lrg_level_get_root (level);
	top = lrg_node_get_children (root);
	for (i = 0; top != NULL && i < top->len; i++)
	{
		LrgNode                *node = g_ptr_array_index (top, i);
		g_autoptr(YamlMapping)  node_map = build_node_mapping (node, storage, NULL);

		yaml_sequence_add_mapping_element (nodes_seq, node_map);
	}

	yaml_mapping_set_sequence_member (root_map, "nodes", nodes_seq);

	/* Additive (0.9.0): levels without an environment keep their shape. */
	if (lrg_level_get_environment (level) != NULL)
	{
		g_autoptr(YamlMapping) env_map =
			lrg_environment_to_yaml_private (lrg_level_get_environment (level));

		yaml_mapping_set_mapping_member (root_map, "environment", env_map);
	}

	/* Additive (0.9.0 M3): the baked-lighting block, only when set. */
	if (lrg_level_has_lighting (level))
	{
		g_autoptr(YamlMapping) lighting_map = build_lighting_mapping (level);

		yaml_mapping_set_mapping_member (root_map, "lighting", lighting_map);
	}

	/* Additive: files without timelines keep their old shape. */
	if (lrg_level_get_n_timelines (level) > 0)
	{
		g_autoptr(YamlSequence) timelines = build_timelines_sequence (level);

		yaml_mapping_set_sequence_member (root_map, "timelines", timelines);
	}

	/* yaml_node_new_mapping refs the mapping (transfer none); let the autoptr
	 * drop our reference so the node is its sole owner. g_steal_pointer here
	 * would leak our reference and thus the entire tree. */
	return yaml_node_new_mapping (root_map);
}

/* ==========================================================================
 * Parse (read) path
 * ========================================================================== */

static LrgMaterial3D *
parse_material (YamlMapping *map)
{
	LrgMaterial3D *material = lrg_material3d_new ();
	YamlNode      *color_node;

	color_node = yaml_mapping_get_member (map, "color");
	if (color_node != NULL && yaml_node_get_node_type (color_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (color_node);

		lrg_material3d_set_color (material,
		                          (gfloat) seq_get_double (seq, 0, 1.0),
		                          (gfloat) seq_get_double (seq, 1, 1.0),
		                          (gfloat) seq_get_double (seq, 2, 1.0),
		                          (gfloat) seq_get_double (seq, 3, 1.0));
	}

	if (yaml_mapping_has_member (map, "roughness"))
		lrg_material3d_set_roughness (material,
		                              (gfloat) yaml_mapping_get_double_member (map, "roughness"));
	if (yaml_mapping_has_member (map, "metallic"))
		lrg_material3d_set_metallic (material,
		                             (gfloat) yaml_mapping_get_double_member (map, "metallic"));

	/* emission: [r, g, b] and emission_strength (0.9.0) */
	color_node = yaml_mapping_get_member (map, "emission");
	if (color_node != NULL && yaml_node_get_node_type (color_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (color_node);

		lrg_material3d_set_emission_color (material,
		                                   (gfloat) seq_get_double (seq, 0, 0.0),
		                                   (gfloat) seq_get_double (seq, 1, 0.0),
		                                   (gfloat) seq_get_double (seq, 2, 0.0),
		                                   1.0f);
	}
	if (yaml_mapping_has_member (map, "emission_strength"))
		lrg_material3d_set_emission_strength (material,
		                                      (gfloat) yaml_mapping_get_double_member (map, "emission_strength"));

	return material;
}

static void
parse_param_bag (YamlMapping *map,
                 void (*setter) (gpointer ctx, const gchar *name, const GValue *value),
                 gpointer ctx)
{
	GList *keys;
	GList *iter;

	keys = yaml_mapping_get_members (map);
	for (iter = keys; iter != NULL; iter = iter->next)
	{
		const gchar *name = iter->data;
		YamlNode    *node = yaml_mapping_get_member (map, name);
		GValue       value = G_VALUE_INIT;

		if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
			continue;

		value_from_scalar (yaml_node_get_scalar (node), &value);
		setter (ctx, name, &value);
		g_value_unset (&value);
	}
	g_list_free (keys);
}

static void
visual_param_setter (gpointer ctx, const gchar *name, const GValue *value)
{
	lrg_node_visual_set_param (LRG_NODE_VISUAL (ctx), name, value);
}

static void
component_prop_setter (gpointer ctx, const gchar *name, const GValue *value)
{
	lrg_component_desc_set_value (LRG_COMPONENT_DESC (ctx), name, value);
}

/* Reads a "tilemap" block; malformed rows are padded/truncated, never
 * fatal, so a hand-edited file still loads. */
static LrgNodeTilemap *
parse_tilemap (YamlMapping *map)
{
	LrgNodeTilemap *tilemap;
	gint64          w, h, tw, th;
	YamlNode       *layers_node;

	w = yaml_mapping_has_member (map, "width") ? yaml_mapping_get_int_member (map, "width") : 0;
	h = yaml_mapping_has_member (map, "height") ? yaml_mapping_get_int_member (map, "height") : 0;
	if (w <= 0 || h <= 0 || w > LRG_NODE_TILEMAP_MAX_SIZE || h > LRG_NODE_TILEMAP_MAX_SIZE)
		return NULL;

	tilemap = lrg_node_tilemap_new ((guint) w, (guint) h);
	tw = yaml_mapping_has_member (map, "tile_width") ? yaml_mapping_get_int_member (map, "tile_width") : 16;
	th = yaml_mapping_has_member (map, "tile_height") ? yaml_mapping_get_int_member (map, "tile_height") : 16;
	if (tw > 0 && th > 0)
		lrg_node_tilemap_set_tile_size (tilemap, (guint) tw, (guint) th);

	layers_node = yaml_mapping_get_member (map, "layers");
	if (layers_node != NULL && yaml_node_get_node_type (layers_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (layers_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode    *ln = yaml_sequence_get_element (seq, i);
			YamlMapping *lmap;
			YamlNode    *rows_node;
			guint        layer;

			if (ln == NULL || yaml_node_get_node_type (ln) != YAML_NODE_MAPPING)
				continue;
			lmap = yaml_node_get_mapping (ln);

			/* The constructor made layer 0; reuse it for the first entry. */
			if (i == 0)
			{
				layer = 0;
				if (yaml_mapping_get_string_member (lmap, "name") != NULL)
					lrg_node_tilemap_set_layer_name (tilemap, 0, yaml_mapping_get_string_member (lmap, "name"));
			}
			else
				layer = lrg_node_tilemap_add_layer (tilemap, yaml_mapping_get_string_member (lmap, "name"));
			if (yaml_mapping_has_member (lmap, "visible"))
				lrg_node_tilemap_set_layer_visible (tilemap, layer,
				                                    yaml_mapping_get_boolean_member (lmap, "visible"));

			rows_node = yaml_mapping_get_member (lmap, "rows");
			if (rows_node != NULL && yaml_node_get_node_type (rows_node) == YAML_NODE_SEQUENCE)
			{
				YamlSequence *rows = yaml_node_get_sequence (rows_node);
				guint         y, nrows = MIN (yaml_sequence_get_length (rows), (guint) h);

				for (y = 0; y < nrows; y++)
				{
					const gchar *row = yaml_sequence_get_string_element (rows, y);
					const gchar *p = row;
					gint         x = 0;

					while (p != NULL && *p != '\0' && x < (gint) w)
					{
						gchar   *end = NULL;
						guint64  id;

						while (*p == ' ' || *p == '\t' || *p == ',')
							p++;
						if (*p == '\0')
							break;
						id = g_ascii_strtoull (p, &end, 10);
						if (end == p)
							break;
						lrg_node_tilemap_set_tile (tilemap, layer, x++, (gint) y, (guint) MIN (id, G_MAXUINT));
						p = end;
					}
				}
			}
		}
	}
	return tilemap;
}

static LrgNodeVisual *
parse_visual (YamlMapping *map)
{
	LrgNodeVisual     *visual;
	LrgNodeVisualKind  kind;
	const gchar       *asset;
	YamlNode          *material_node;
	YamlNode          *params_node;
	YamlNode          *tilemap_node;

	kind = nick_to_enum (LRG_TYPE_NODE_VISUAL_KIND,
	                     yaml_mapping_get_string_member (map, "kind"),
	                     LRG_NODE_VISUAL_NONE);
	visual = lrg_node_visual_new (kind);

	if (yaml_mapping_has_member (map, "primitive"))
		lrg_node_visual_set_primitive (visual,
		    nick_to_enum (LRG_TYPE_PRIMITIVE_TYPE,
		                  yaml_mapping_get_string_member (map, "primitive"),
		                  LRG_PRIMITIVE_CUBE));

	asset = yaml_mapping_get_string_member (map, "asset");
	if (asset != NULL)
		lrg_node_visual_set_asset (visual, asset);

	/* material: an inline block, or a .rmat path / guid: reference */
	material_node = yaml_mapping_get_member (map, "material");
	if (material_node != NULL && yaml_node_get_node_type (material_node) == YAML_NODE_MAPPING)
	{
		g_autoptr(LrgMaterial3D) material = parse_material (yaml_node_get_mapping (material_node));
		lrg_node_visual_set_material (visual, material);
	}
	else if (material_node != NULL && yaml_node_get_node_type (material_node) == YAML_NODE_SCALAR)
		lrg_node_visual_set_material_ref (visual, yaml_node_get_scalar (material_node));

	params_node = yaml_mapping_get_member (map, "params");
	if (params_node != NULL && yaml_node_get_node_type (params_node) == YAML_NODE_MAPPING)
		parse_param_bag (yaml_node_get_mapping (params_node), visual_param_setter, visual);

	tilemap_node = yaml_mapping_get_member (map, "tilemap");
	if (tilemap_node != NULL && yaml_node_get_node_type (tilemap_node) == YAML_NODE_MAPPING)
	{
		g_autoptr(LrgNodeTilemap) tilemap = parse_tilemap (yaml_node_get_mapping (tilemap_node));

		if (tilemap != NULL)
			lrg_node_visual_set_tilemap (visual, tilemap);
	}

	return visual;
}

static LrgComponentDesc *
parse_component (YamlMapping *map)
{
	LrgComponentDesc *desc;
	YamlNode         *props_node;

	desc = lrg_component_desc_new (yaml_mapping_get_string_member (map, "type"));

	props_node = yaml_mapping_get_member (map, "props");
	if (props_node != NULL && yaml_node_get_node_type (props_node) == YAML_NODE_MAPPING)
		parse_param_bag (yaml_node_get_mapping (props_node), component_prop_setter, desc);

	return desc;
}

static LrgScriptBinding *
parse_script (YamlMapping *map)
{
	LrgScriptBinding  *binding;
	LrgScriptLanguage  lang;

	lang = nick_to_enum (LRG_TYPE_SCRIPT_LANGUAGE,
	                     yaml_mapping_get_string_member (map, "language"),
	                     LRG_SCRIPT_LANGUAGE_NONE);
	binding = lrg_script_binding_new (lang,
	                                  yaml_mapping_get_string_member (map, "script"));

	if (yaml_mapping_has_member (map, "enabled"))
		lrg_script_binding_set_enabled (binding,
		                                yaml_mapping_get_boolean_member (map, "enabled"));

	return binding;
}

/* Reads a `prefab:` block (see build_prefab_mapping()); NULL when it
 * has no source. */
static LrgPrefabLink *
parse_prefab (YamlMapping *map)
{
	LrgPrefabLink *link;
	const gchar   *source = yaml_mapping_get_string_member (map, "source");
	YamlNode      *map_node;
	YamlNode      *ov_node;

	if (source == NULL)
		return NULL;

	link = lrg_prefab_link_new (source, yaml_mapping_get_string_member (map, "guid"));

	map_node = yaml_mapping_get_member (map, "map");
	if (map_node != NULL && yaml_node_get_node_type (map_node) == YAML_NODE_MAPPING)
	{
		YamlMapping *gm = yaml_node_get_mapping (map_node);
		GList       *keys = yaml_mapping_get_members (gm);
		GList       *l;

		for (l = keys; l != NULL; l = l->next)
		{
			const gchar *src_guid = yaml_mapping_get_string_member (gm, l->data);

			if (src_guid != NULL)
				lrg_prefab_link_map_set (link, l->data, src_guid);
		}
		g_list_free (keys);
	}

	ov_node = yaml_mapping_get_member (map, "overrides");
	if (ov_node != NULL && yaml_node_get_node_type (ov_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (ov_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			const gchar *key = yaml_sequence_get_string_element (seq, i);

			if (key != NULL && *key != '\0')
				lrg_prefab_link_add_override (link, key);
		}
	}

	return link;
}

/*
 * ParseCtx: what one load needs to expand reference-stored instances.
 */
typedef struct
{
	LrgLevelSerializer *self;
	gchar              *level_dir;      /* nullable (data loads) */
	gchar              *project_root;   /* nullable */
	LrgAssetManager    *guid_index;     /* lazily made from project_root */
	GHashTable         *prefabs;        /* canonical path -> LrgNode* (resolved) */
} ParseCtx;

static void
parse_ctx_init (ParseCtx           *ctx,
                LrgLevelSerializer *self,
                const gchar        *level_path)
{
	ctx->self = self;
	ctx->level_dir = (level_path != NULL) ? g_path_get_dirname (level_path) : NULL;
	ctx->project_root = effective_project_root (self, level_path);
	ctx->guid_index = NULL;
	ctx->prefabs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

static void
parse_ctx_clear (ParseCtx *ctx)
{
	g_clear_pointer (&ctx->level_dir, g_free);
	g_clear_pointer (&ctx->project_root, g_free);
	g_clear_object (&ctx->guid_index);
	g_clear_pointer (&ctx->prefabs, g_hash_table_unref);
}

/* Resolves a guid through the serializer's asset manager, else through a
 * guid index of the project root (built once per load). */
static gchar *
ctx_resolve_guid (ParseCtx    *ctx,
                  const gchar *guid)
{
	if (guid == NULL || *guid == '\0')
		return NULL;
	if (ctx->self->asset_manager != NULL)
		return lrg_asset_manager_resolve_guid (ctx->self->asset_manager, guid);
	if (ctx->project_root == NULL)
		return NULL;
	if (ctx->guid_index == NULL)
	{
		ctx->guid_index = lrg_asset_manager_new ();
		lrg_asset_manager_set_project_root (ctx->guid_index, ctx->project_root);
	}
	return lrg_asset_manager_resolve_guid (ctx->guid_index, guid);
}

/*
 * ctx_resolve_source:
 *
 * Finds the `.rprefab` of a reference-stored instance: by asset guid (the
 * link's `guid`, or a `guid:` source), then the path as given (absolute,
 * or relative to the current directory as the editor writes it), then
 * relative to the project root and to the level's directory.
 */
static gchar *
ctx_resolve_source (ParseCtx      *ctx,
                    LrgPrefabLink *link)
{
	const gchar *source = lrg_prefab_link_get_source_path (link);
	const gchar *bases[2];
	gchar       *found;
	guint        i;

	if (source != NULL && g_str_has_prefix (source, LRG_ASSET_GUID_PREFIX))
	{
		found = ctx_resolve_guid (ctx, source + strlen (LRG_ASSET_GUID_PREFIX));
		if (found != NULL)
			return found;
	}
	found = ctx_resolve_guid (ctx, lrg_prefab_link_get_source_guid (link));
	if (found != NULL)
		return found;
	if (source == NULL)
		return NULL;

	if (g_file_test (source, G_FILE_TEST_IS_REGULAR))
		return g_strdup (source);
	if (g_path_is_absolute (source))
		return NULL;

	bases[0] = ctx->project_root;
	bases[1] = ctx->level_dir;
	for (i = 0; i < G_N_ELEMENTS (bases); i++)
	{
		g_autofree gchar *candidate = NULL;

		if (bases[i] == NULL)
			continue;
		candidate = g_build_filename (bases[i], source, NULL);
		if (g_file_test (candidate, G_FILE_TEST_IS_REGULAR))
			return g_steal_pointer (&candidate);
	}
	return NULL;
}

/*
 * expand_reference:
 *
 * Replaces a parsed sparse instance by the full linked instance built
 * from its (resolved, cached) source. When the source cannot be found or
 * loaded the sparse node is kept as it is (with its link, so the editor
 * can relink it) and a message is logged: a missing prefab never makes
 * the level unloadable.
 */
static LrgNode *
expand_reference (ParseCtx *ctx,
                  LrgNode  *sparse)
{
	LrgPrefabLink     *link = lrg_node_get_prefab_link (sparse);
	g_autofree gchar  *path = ctx_resolve_source (ctx, link);
	g_autofree gchar  *canon = NULL;
	g_autoptr(GError)  error = NULL;
	LrgNode           *prefab;
	LrgNode           *full;

	if (path == NULL)
	{
		g_message ("Prefab %s of reference-stored instance %s not found; keeping the reference",
		           lrg_prefab_link_get_source_path (link), lrg_node_get_guid (sparse));
		return g_object_ref (sparse);
	}

	canon = g_canonicalize_filename (path, NULL);
	prefab = g_hash_table_lookup (ctx->prefabs, canon);
	if (prefab == NULL)
	{
		prefab = lrg_prefab_load_resolved (path, NULL, &error);
		if (prefab == NULL)
		{
			g_message ("Cannot load prefab %s for instance %s: %s; keeping the reference",
			           path, lrg_node_get_guid (sparse), error->message);
			return g_object_ref (sparse);
		}
		g_hash_table_insert (ctx->prefabs, g_strdup (canon), prefab);
	}

	full = lrg_prefab_instantiate_reference (sparse, prefab, &error);
	if (full == NULL)
	{
		g_message ("Cannot expand prefab reference %s: %s", lrg_node_get_guid (sparse), error->message);
		return g_object_ref (sparse);
	}
	return full;
}

static LrgNode *
parse_node (YamlMapping *map,
            ParseCtx    *ctx)
{
	LrgNode  *node;
	const gchar *guid;
	YamlNode *transform_node;
	YamlNode *visual_node;
	YamlNode *components_node;
	YamlNode *physics_node;
	YamlNode *scripts_node;
	YamlNode *children_node;
	YamlNode *prefab_node;
	gboolean  is_reference = FALSE;

	node = lrg_node_new (yaml_mapping_get_string_member (map, "name"));

	guid = yaml_mapping_get_string_member (map, "guid");
	if (guid != NULL)
		lrg_node_set_guid (node, guid);

	if (yaml_mapping_has_member (map, "is_2d"))
		lrg_node_set_is_2d (node, yaml_mapping_get_boolean_member (map, "is_2d"));
	if (yaml_mapping_has_member (map, "visible"))
		lrg_node_set_visible (node, yaml_mapping_get_boolean_member (map, "visible"));
	if (yaml_mapping_has_member (map, "locked"))
		lrg_node_set_locked (node, yaml_mapping_get_boolean_member (map, "locked"));

	transform_node = yaml_mapping_get_member (map, "transform");
	if (transform_node != NULL && yaml_node_get_node_type (transform_node) == YAML_NODE_MAPPING)
	{
		YamlMapping *tmap = yaml_node_get_mapping (transform_node);
		YamlNode    *loc = yaml_mapping_get_member (tmap, "location");
		YamlNode    *rot = yaml_mapping_get_member (tmap, "rotation");
		YamlNode    *scl = yaml_mapping_get_member (tmap, "scale");

		if (loc != NULL && yaml_node_get_node_type (loc) == YAML_NODE_SEQUENCE)
		{
			YamlSequence *s = yaml_node_get_sequence (loc);
			lrg_node_set_location_xyz (node,
			                           (gfloat) seq_get_double (s, 0, 0.0),
			                           (gfloat) seq_get_double (s, 1, 0.0),
			                           (gfloat) seq_get_double (s, 2, 0.0));
		}
		if (rot != NULL && yaml_node_get_node_type (rot) == YAML_NODE_SEQUENCE)
		{
			YamlSequence *s = yaml_node_get_sequence (rot);
			lrg_node_set_rotation_xyz (node,
			                           (gfloat) seq_get_double (s, 0, 0.0),
			                           (gfloat) seq_get_double (s, 1, 0.0),
			                           (gfloat) seq_get_double (s, 2, 0.0));
		}
		if (scl != NULL && yaml_node_get_node_type (scl) == YAML_NODE_SEQUENCE)
		{
			YamlSequence *s = yaml_node_get_sequence (scl);
			lrg_node_set_scale_xyz (node,
			                        (gfloat) seq_get_double (s, 0, 1.0),
			                        (gfloat) seq_get_double (s, 1, 1.0),
			                        (gfloat) seq_get_double (s, 2, 1.0));
		}
	}

	visual_node = yaml_mapping_get_member (map, "visual");
	if (visual_node != NULL && yaml_node_get_node_type (visual_node) == YAML_NODE_MAPPING)
	{
		g_autoptr(LrgNodeVisual) visual = parse_visual (yaml_node_get_mapping (visual_node));
		lrg_node_set_visual (node, visual);
	}

	physics_node = yaml_mapping_get_member (map, "physics");
	if (physics_node != NULL && yaml_node_get_node_type (physics_node) == YAML_NODE_MAPPING)
	{
		g_autoptr(LrgNodePhysics) physics = parse_physics (yaml_node_get_mapping (physics_node));

		lrg_node_set_physics (node, physics);
	}

	components_node = yaml_mapping_get_member (map, "components");
	if (components_node != NULL && yaml_node_get_node_type (components_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (components_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode *cn = yaml_sequence_get_element (seq, i);

			if (cn != NULL && yaml_node_get_node_type (cn) == YAML_NODE_MAPPING)
			{
				g_autoptr(LrgComponentDesc) desc = parse_component (yaml_node_get_mapping (cn));
				lrg_node_add_component (node, desc);
			}
		}
	}

	scripts_node = yaml_mapping_get_member (map, "scripts");
	if (scripts_node != NULL && yaml_node_get_node_type (scripts_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (scripts_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode *sn = yaml_sequence_get_element (seq, i);

			if (sn != NULL && yaml_node_get_node_type (sn) == YAML_NODE_MAPPING)
			{
				g_autoptr(LrgScriptBinding) binding = parse_script (yaml_node_get_mapping (sn));
				lrg_node_add_script (node, binding);
			}
		}
	}

	prefab_node = yaml_mapping_get_member (map, "prefab");
	if (prefab_node != NULL && yaml_node_get_node_type (prefab_node) == YAML_NODE_MAPPING)
	{
		YamlMapping              *pm = yaml_node_get_mapping (prefab_node);
		g_autoptr(LrgPrefabLink)  link = parse_prefab (pm);

		lrg_node_set_prefab_link (node, link);
		is_reference = link != NULL &&
		               g_strcmp0 (yaml_mapping_get_string_member (pm, "storage"), "reference") == 0;
	}

	children_node = yaml_mapping_get_member (map, "children");
	if (children_node != NULL && yaml_node_get_node_type (children_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (children_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode *cn = yaml_sequence_get_element (seq, i);

			if (cn != NULL && yaml_node_get_node_type (cn) == YAML_NODE_MAPPING)
			{
				g_autoptr(LrgNode) child = parse_node (yaml_node_get_mapping (cn), ctx);
				lrg_node_add_child (node, child);
			}
		}
	}

	/* A reference-stored instance becomes the full linked instance. */
	if (is_reference && ctx != NULL)
	{
		g_autoptr(LrgNode) sparse = node;

		return expand_reference (ctx, sparse);
	}

	return node;
}

static gdouble
mapping_double (YamlMapping *map,
                const gchar *key,
                gdouble      def)
{
	return yaml_mapping_has_member (map, key) ? yaml_mapping_get_double_member (map, key) : def;
}

static void
parse_timelines (LrgLevel     *level,
                 YamlSequence *seq)
{
	guint i, j, k;

	for (i = 0; i < yaml_sequence_get_length (seq); i++)
	{
		YamlNode              *tn = yaml_sequence_get_element (seq, i);
		YamlMapping           *map;
		YamlNode              *tracks_node;
		const gchar           *name;
		gdouble                length;
		g_autoptr(LrgTimeline) tl = NULL;

		if (tn == NULL || yaml_node_get_node_type (tn) != YAML_NODE_MAPPING)
			continue;
		map = yaml_node_get_mapping (tn);
		name = yaml_mapping_get_string_member (map, "name");
		if (name == NULL || lrg_level_find_timeline (level, name) >= 0)
			continue;

		tl = lrg_timeline_new (name);
		length = mapping_double (map, "length", 2.0);
		if (length > 0.0)
			lrg_timeline_set_length (tl, (gfloat) length);
		if (yaml_mapping_has_member (map, "loop"))
			lrg_timeline_set_loop (tl, yaml_mapping_get_boolean_member (map, "loop"));
		if (yaml_mapping_has_member (map, "autoplay"))
			lrg_timeline_set_autoplay (tl, yaml_mapping_get_boolean_member (map, "autoplay"));

		tracks_node = yaml_mapping_get_member (map, "tracks");
		if (tracks_node != NULL && yaml_node_get_node_type (tracks_node) == YAML_NODE_SEQUENCE)
		{
			YamlSequence *tracks = yaml_node_get_sequence (tracks_node);

			for (j = 0; j < yaml_sequence_get_length (tracks); j++)
			{
				YamlNode         *trn = yaml_sequence_get_element (tracks, j);
				YamlMapping      *tmap;
				YamlNode         *keys_node;
				const gchar      *guid, *channel;
				LrgTimelineTrack *track;

				if (trn == NULL || yaml_node_get_node_type (trn) != YAML_NODE_MAPPING)
					continue;
				tmap = yaml_node_get_mapping (trn);
				guid = yaml_mapping_get_string_member (tmap, "node");
				channel = yaml_mapping_get_string_member (tmap, "channel");
				if (guid == NULL || !lrg_timeline_channel_is_valid (channel))
					continue;
				track = lrg_timeline_ensure_track (tl, guid, channel);

				keys_node = yaml_mapping_get_member (tmap, "keys");
				if (keys_node == NULL || yaml_node_get_node_type (keys_node) != YAML_NODE_SEQUENCE)
					continue;
				for (k = 0; k < yaml_sequence_get_length (yaml_node_get_sequence (keys_node)); k++)
				{
					YamlNode    *kn = yaml_sequence_get_element (yaml_node_get_sequence (keys_node), k);
					YamlMapping *kmap;
					gfloat       kt;
					gint         kidx;

					if (kn == NULL || yaml_node_get_node_type (kn) != YAML_NODE_MAPPING)
						continue;
					kmap = yaml_node_get_mapping (kn);
					kt = (gfloat) mapping_double (kmap, "time", 0.0);
					lrg_timeline_track_set_key (track, kt,
					                            (gfloat) mapping_double (kmap, "value", 0.0),
					                            nick_to_enum (LRG_TYPE_EASING_TYPE,
					                                          yaml_mapping_get_string_member (kmap, "easing"),
					                                          LRG_EASING_LINEAR));
					/* Bézier keys (0.9.0): "tangent" makes the key a Bézier key */
					kidx = lrg_timeline_track_find_key (track, MAX (kt, 0.0f), 0.0005f);
					if (kidx >= 0 && yaml_mapping_has_member (kmap, "tangent"))
					{
						LrgSequenceTangentMode tmode;
						gdouble                slope = mapping_double (kmap, "slope", 0.0);

						tmode = (LrgSequenceTangentMode) nick_to_enum (LRG_TYPE_SEQUENCE_TANGENT_MODE,
						                                                 yaml_mapping_get_string_member (kmap, "tangent"),
						                                                 LRG_SEQUENCE_TANGENT_AUTO);
						lrg_timeline_track_set_key_tangents (track, (guint) kidx, tmode,
						                                     (gfloat) mapping_double (kmap, "in", slope),
						                                     (gfloat) mapping_double (kmap, "out", slope));
					}
				}
			}
		}
		lrg_level_insert_timeline (level, tl, -1);
	}
}

static LrgLevel *
parse_level_from_root (YamlNode  *root,
                       ParseCtx  *ctx,
                       GError   **error)
{
	YamlMapping *root_map;
	LrgLevel    *level;
	const gchar *name;
	const gchar *view;
	YamlNode    *nodes_node;
	YamlNode    *timelines_node;
	YamlNode    *env_node;

	if (yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
		             "Level root is not a mapping");
		return NULL;
	}

	root_map = yaml_node_get_mapping (root);
	name = yaml_mapping_get_string_member (root_map, "name");
	level = lrg_level_new (name);

	view = yaml_mapping_get_string_member (root_map, "default_view");
	if (view != NULL && g_ascii_strcasecmp (view, "2d") == 0)
		lrg_level_set_default_2d (level, TRUE);

	/* The level's own prefab storage setting (0.9.0). */
	if (yaml_mapping_has_member (root_map, STORAGE_KEY))
		lrg_prefab_level_set_storage (level, lrg_prefab_storage_from_string (
			yaml_mapping_get_string_member (root_map, STORAGE_KEY)));
	else if (yaml_mapping_has_member (root_map, STORAGE_KEY_ALT))
		lrg_prefab_level_set_storage (level, lrg_prefab_storage_from_string (
			yaml_mapping_get_string_member (root_map, STORAGE_KEY_ALT)));

	nodes_node = yaml_mapping_get_member (root_map, "nodes");
	if (nodes_node != NULL && yaml_node_get_node_type (nodes_node) == YAML_NODE_SEQUENCE)
	{
		YamlSequence *seq = yaml_node_get_sequence (nodes_node);
		guint         i, n = yaml_sequence_get_length (seq);

		for (i = 0; i < n; i++)
		{
			YamlNode *cn = yaml_sequence_get_element (seq, i);

			if (cn != NULL && yaml_node_get_node_type (cn) == YAML_NODE_MAPPING)
			{
				g_autoptr(LrgNode) node = parse_node (yaml_node_get_mapping (cn), ctx);
				lrg_level_add_node (level, node, NULL);
			}
		}
	}

	timelines_node = yaml_mapping_get_member (root_map, "timelines");
	if (timelines_node != NULL && yaml_node_get_node_type (timelines_node) == YAML_NODE_SEQUENCE)
		parse_timelines (level, yaml_node_get_sequence (timelines_node));

	/* The environment block (0.9.0); a malformed one fails the load. */
	env_node = yaml_mapping_get_member (root_map, "environment");
	if (env_node != NULL && yaml_node_get_node_type (env_node) == YAML_NODE_MAPPING)
	{
		g_autoptr(LrgEnvironment) env = lrg_environment_new ();
		g_autoptr(GError)         env_error = NULL;

		if (!lrg_environment_from_yaml_private (env, yaml_node_get_mapping (env_node), &env_error))
		{
			g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
			             "environment: %s", env_error->message);
			g_object_unref (level);
			return NULL;
		}
		lrg_level_set_environment (level, env);
	}

	/* The baked-lighting block (0.9.0 M3); a malformed one fails the load. */
	{
		YamlNode         *lighting_node = yaml_mapping_get_member (root_map, "lighting");
		g_autoptr(GError) lighting_error = NULL;

		if (lighting_node != NULL && !yaml_node_is_null (lighting_node)
		    && !parse_lighting (level, lighting_node, &lighting_error))
		{
			g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
			             "lighting: %s", lighting_error->message);
			g_object_unref (level);
			return NULL;
		}
	}

	return level;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgLevelSerializer *
lrg_level_serializer_new (void)
{
	return g_object_new (LRG_TYPE_LEVEL_SERIALIZER, NULL);
}

LrgLevel *
lrg_level_serializer_load_from_file (LrgLevelSerializer  *self,
                                     const gchar         *path,
                                     GError             **error)
{
	g_autoptr(YamlParser) parser = NULL;
	YamlNode             *root;
	ParseCtx              ctx;
	LrgLevel             *level;

	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, path, error))
		return NULL;

	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
		             "No YAML document found in file");
		return NULL;
	}

	parse_ctx_init (&ctx, self, path);
	level = parse_level_from_root (root, &ctx, error);
	parse_ctx_clear (&ctx);
	return level;
}

LrgLevel *
lrg_level_serializer_load_from_data (LrgLevelSerializer  *self,
                                     const gchar         *data,
                                     gssize               length,
                                     GError             **error)
{
	g_autoptr(YamlParser) parser = NULL;
	YamlNode             *root;
	ParseCtx              ctx;
	LrgLevel             *level;

	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), NULL);
	g_return_val_if_fail (data != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_data (parser, data, length, error))
		return NULL;

	root = yaml_parser_get_root (parser);
	if (root == NULL)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_PARSE,
		             "No YAML document found in data");
		return NULL;
	}

	parse_ctx_init (&ctx, self, NULL);
	level = parse_level_from_root (root, &ctx, error);
	parse_ctx_clear (&ctx);
	return level;
}

gboolean
lrg_level_serializer_save_to_file (LrgLevelSerializer  *self,
                                   LrgLevel            *level,
                                   const gchar         *path,
                                   GError             **error)
{
	g_autofree gchar *data = NULL;
	gsize             length = 0;

	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), FALSE);
	g_return_val_if_fail (LRG_IS_LEVEL (level), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	data = save_to_data_internal (self, level, path, &length);
	if (data == NULL)
	{
		g_set_error (error, LRG_LEVEL_ERROR, LRG_LEVEL_ERROR_FAILED,
		             "Failed to serialize level");
		return FALSE;
	}

	return g_file_set_contents (path, data, (gssize) length, error);
}

/* Serializes with the storage mode that applies to @path (nullable). */
static gchar *
save_to_data_internal (LrgLevelSerializer *self,
                       LrgLevel           *level,
                       const gchar        *path,
                       gsize              *length)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;

	root = build_level_yaml (level, effective_storage (self, level, path));
	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();

	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, length, NULL);
}

gchar *
lrg_level_serializer_save_to_data (LrgLevelSerializer *self,
                                   LrgLevel           *level,
                                   gsize              *length)
{
	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), NULL);
	g_return_val_if_fail (LRG_IS_LEVEL (level), NULL);

	return save_to_data_internal (self, level, NULL, length);
}

/* ==========================================================================
 * Properties (since 0.9.0)
 * ========================================================================== */

void
lrg_level_serializer_set_prefab_storage (LrgLevelSerializer *self,
                                         LrgPrefabStorage    storage)
{
	g_return_if_fail (LRG_IS_LEVEL_SERIALIZER (self));

	if (self->prefab_storage == storage)
		return;
	self->prefab_storage = storage;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PREFAB_STORAGE]);
}

LrgPrefabStorage
lrg_level_serializer_get_prefab_storage (LrgLevelSerializer *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), LRG_PREFAB_STORAGE_DEFAULT);
	return self->prefab_storage;
}

void
lrg_level_serializer_set_project_root (LrgLevelSerializer *self,
                                       const gchar        *root)
{
	g_return_if_fail (LRG_IS_LEVEL_SERIALIZER (self));

	if (g_strcmp0 (self->project_root, root) == 0)
		return;
	g_free (self->project_root);
	self->project_root = g_strdup (root);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROJECT_ROOT]);
}

const gchar *
lrg_level_serializer_get_project_root (LrgLevelSerializer *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), NULL);
	return self->project_root;
}

void
lrg_level_serializer_set_asset_manager (LrgLevelSerializer *self,
                                        LrgAssetManager    *manager)
{
	g_return_if_fail (LRG_IS_LEVEL_SERIALIZER (self));
	g_return_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager));

	if (g_set_object (&self->asset_manager, manager))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ASSET_MANAGER]);
}

LrgAssetManager *
lrg_level_serializer_get_asset_manager (LrgLevelSerializer *self)
{
	g_return_val_if_fail (LRG_IS_LEVEL_SERIALIZER (self), NULL);
	return self->asset_manager;
}
