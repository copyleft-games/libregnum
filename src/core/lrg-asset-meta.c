/* lrg-asset-meta.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAssetMeta: the "<file>.meta" YAML sidecar of an asset.
 *
 * The whole document is kept as a mutable yaml-glib mapping (self->root),
 * so every key the editor does not understand survives a load/save round
 * trip untouched. The well-known keys are read and written in place:
 *
 *   version:  1
 *   guid:     <uuid v4, or the legacy sha256 of the relative path>
 *   importer: texture | model | audio | font | script | level | ...
 *   settings: { importer specific keys }
 *
 * A static table (setting_specs) declares the settings each importer
 * knows: their type, default, allowed choices and numeric range. It drives
 * the defaults returned by the getters, the validation in
 * lrg_asset_meta_set_setting_variant(), the property-grid helpers and the
 * settings hash used by the thumbnail cache.
 */

#include "config.h"

#include "lrg-asset-meta.h"
#include <gio/gio.h>
#include <yaml-glib.h>
#include <errno.h>
#include <string.h>

/* ==========================================================================
 * Enum types (hand-written; lrg-enums is not ours to touch)
 * ========================================================================== */

GType
lrg_asset_texture_filter_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ASSET_TEXTURE_FILTER_POINT, "LRG_ASSET_TEXTURE_FILTER_POINT", "point" },
			{ LRG_ASSET_TEXTURE_FILTER_BILINEAR, "LRG_ASSET_TEXTURE_FILTER_BILINEAR", "bilinear" },
			{ LRG_ASSET_TEXTURE_FILTER_TRILINEAR, "LRG_ASSET_TEXTURE_FILTER_TRILINEAR", "trilinear" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgAssetTextureFilter"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_asset_texture_wrap_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ASSET_TEXTURE_WRAP_REPEAT, "LRG_ASSET_TEXTURE_WRAP_REPEAT", "repeat" },
			{ LRG_ASSET_TEXTURE_WRAP_CLAMP, "LRG_ASSET_TEXTURE_WRAP_CLAMP", "clamp" },
			{ LRG_ASSET_TEXTURE_WRAP_MIRROR, "LRG_ASSET_TEXTURE_WRAP_MIRROR", "mirror" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgAssetTextureWrap"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_asset_up_axis_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ASSET_UP_AXIS_Y, "LRG_ASSET_UP_AXIS_Y", "y" },
			{ LRG_ASSET_UP_AXIS_Z, "LRG_ASSET_UP_AXIS_Z", "z" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgAssetUpAxis"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_asset_audio_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_ASSET_AUDIO_MODE_STREAM, "LRG_ASSET_AUDIO_MODE_STREAM", "stream" },
			{ LRG_ASSET_AUDIO_MODE_SAMPLE, "LRG_ASSET_AUDIO_MODE_SAMPLE", "sample" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgAssetAudioMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Setting specifications
 * ========================================================================== */

typedef enum
{
	SPEC_BOOL,
	SPEC_INT,
	SPEC_DOUBLE,
	SPEC_STRING,
	SPEC_CHOICE
} SpecKind;

typedef struct
{
	const gchar *importer;
	const gchar *key;
	SpecKind     kind;
	const gchar *def;       /* default, as YAML scalar text */
	GType      (*choices) (void);   /* SPEC_CHOICE: enum whose nicks are the values */
	gdouble      min;
	gdouble      max;
} SettingSpec;

static const SettingSpec setting_specs[] = {
	{ "texture", "filter", SPEC_CHOICE, "bilinear", lrg_asset_texture_filter_get_type, 0, 0 },
	{ "texture", "mipmaps", SPEC_BOOL, "false", NULL, 0, 0 },
	{ "texture", "wrap", SPEC_CHOICE, "repeat", lrg_asset_texture_wrap_get_type, 0, 0 },
	{ "texture", "srgb", SPEC_BOOL, "true", NULL, 0, 0 },
	{ "texture", "max-size", SPEC_INT, "0", NULL, 0, 16384 },
	{ "model", "scale", SPEC_DOUBLE, "1", NULL, 1e-6, 1e6 },
	{ "model", "up-axis", SPEC_CHOICE, "y", lrg_asset_up_axis_get_type, 0, 0 },
	{ "model", "generate-normals", SPEC_BOOL, "false", NULL, 0, 0 },
	{ "model", "import-animations", SPEC_BOOL, "true", NULL, 0, 0 },
	/* LOD chain generated at load time (LrgLodGroup, 0.9.0) */
	{ "model", "lod-levels", SPEC_INT, "1", NULL, 1, 8 },
	{ "model", "lod-ratios", SPEC_STRING, "", NULL, 0, 0 },
	{ "model", "lod-screen-sizes", SPEC_STRING, "", NULL, 0, 0 },
	{ "model", "lod-max-error", SPEC_DOUBLE, "0.05", NULL, 0, 1 },
	{ "model", "lod-lock-border", SPEC_BOOL, "true", NULL, 0, 0 },
	{ "model", "lod-hysteresis", SPEC_DOUBLE, "0.1", NULL, 0, 0.9 },
	{ "model", "lod-fade-duration", SPEC_DOUBLE, "0.25", NULL, 0, 10 },
	{ "audio", "mode", SPEC_CHOICE, "sample", lrg_asset_audio_mode_get_type, 0, 0 },
	{ "audio", "loop", SPEC_BOOL, "false", NULL, 0, 0 },
	{ "audio", "volume", SPEC_DOUBLE, "1", NULL, 0, 4 },
	{ "font", "size", SPEC_INT, "32", NULL, 1, 1024 },
	{ "font", "codepoints", SPEC_STRING, "32-126", NULL, 0, 0 }
};

static const SettingSpec *
find_spec (const gchar *importer,
           const gchar *key)
{
	guint i;

	if (importer == NULL || key == NULL)
		return NULL;
	for (i = 0; i < G_N_ELEMENTS (setting_specs); i++)
		if (g_str_equal (setting_specs[i].importer, importer) && g_str_equal (setting_specs[i].key, key))
			return &setting_specs[i];
	return NULL;
}

/* ==========================================================================
 * Scalar text helpers
 * ========================================================================== */

static gboolean
text_is_true (const gchar *text)
{
	return text != NULL && (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "yes") == 0 ||
	                        g_ascii_strcasecmp (text, "on") == 0 || g_strcmp0 (text, "1") == 0);
}

static gboolean
text_is_false (const gchar *text)
{
	return text != NULL && (g_ascii_strcasecmp (text, "false") == 0 || g_ascii_strcasecmp (text, "no") == 0 ||
	                        g_ascii_strcasecmp (text, "off") == 0 || g_strcmp0 (text, "0") == 0);
}

/* Parses a whole string as a base-10 integer. */
static gboolean
text_to_int (const gchar *text,
             gint64      *out)
{
	gchar *end = NULL;
	gint64 v;

	if (text == NULL || *text == '\0')
		return FALSE;
	errno = 0;
	v = g_ascii_strtoll (text, &end, 10);
	if (errno != 0 || end == NULL || *end != '\0')
		return FALSE;
	*out = v;
	return TRUE;
}

/* Parses a whole string as a double (no inf/nan). */
static gboolean
text_to_double (const gchar *text,
                gdouble     *out)
{
	gchar  *end = NULL;
	gdouble v;

	if (text == NULL || *text == '\0')
		return FALSE;
	errno = 0;
	v = g_ascii_strtod (text, &end);
	if (errno != 0 || end == NULL || *end != '\0' || v != v || v > G_MAXDOUBLE || v < -G_MAXDOUBLE)
		return FALSE;
	*out = v;
	return TRUE;
}

/* Shortest decimal text that reads back as exactly @value ("0.1", not
 * "0.10000000000000001"). @buf holds G_ASCII_DTOSTR_BUF_SIZE bytes. */
static const gchar *
format_double (gchar   *buf,
               gdouble  value)
{
	gint prec;

	for (prec = 6; prec < 17; prec++)
	{
		gchar fmt[8];

		g_snprintf (fmt, sizeof (fmt), "%%.%dg", prec);
		g_ascii_formatd (buf, G_ASCII_DTOSTR_BUF_SIZE, fmt, value);
		if (g_ascii_strtod (buf, NULL) == value)
		{
			/* Keep integral values recognisably floating ("1.0") so an
			 * unknown key reads back as a double, not an integer. */
			if (strpbrk (buf, ".eEni") == NULL && strlen (buf) + 2 < G_ASCII_DTOSTR_BUF_SIZE)
				strcat (buf, ".0");
			return buf;
		}
	}
	return g_ascii_dtostr (buf, G_ASCII_DTOSTR_BUF_SIZE, value);
}

/* Text of a scalar node, NULL for anything else. */
static const gchar *
node_text (YamlNode *node)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return NULL;
	return yaml_node_get_scalar (node);
}

/* A YAML sequence of scalars as "a, b, c" (non-scalars are skipped), so
 * list settings may be written either as a flow sequence or a string. */
static gchar *
sequence_text (YamlNode *node)
{
	g_autoptr(GString) out = g_string_new (NULL);
	YamlSequence      *seq = yaml_node_get_sequence (node);
	guint              i;

	for (i = 0; seq != NULL && i < yaml_sequence_get_length (seq); i++)
	{
		const gchar *text = node_text (yaml_sequence_get_element (seq, i));

		if (text == NULL)
			continue;
		if (out->len > 0)
			g_string_append (out, ", ");
		g_string_append (out, text);
	}
	return g_string_free (g_steal_pointer (&out), FALSE);
}

/* ==========================================================================
 * Object
 * ========================================================================== */

struct _LrgAssetMeta
{
	GObject parent_instance;

	YamlMapping *root;    /* the whole document, unknown keys included */
	gboolean     dirty;
};

enum
{
	PROP_0,
	PROP_GUID,
	PROP_IMPORTER,
	N_PROPS
};

enum
{
	SIGNAL_SETTING_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgAssetMeta, lrg_asset_meta, G_TYPE_OBJECT)

static void
lrg_asset_meta_finalize (GObject *object)
{
	LrgAssetMeta *self = LRG_ASSET_META (object);

	g_clear_pointer (&self->root, yaml_mapping_unref);

	G_OBJECT_CLASS (lrg_asset_meta_parent_class)->finalize (object);
}

static void
lrg_asset_meta_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgAssetMeta *self = LRG_ASSET_META (object);

	switch (prop_id)
	{
	case PROP_GUID:
		g_value_set_string (value, lrg_asset_meta_get_guid (self));
		break;
	case PROP_IMPORTER:
		g_value_set_string (value, lrg_asset_meta_get_importer (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_asset_meta_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgAssetMeta *self = LRG_ASSET_META (object);

	switch (prop_id)
	{
	case PROP_GUID:
		lrg_asset_meta_set_guid (self, g_value_get_string (value));
		break;
	case PROP_IMPORTER:
		lrg_asset_meta_set_importer (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_asset_meta_class_init (LrgAssetMetaClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_asset_meta_finalize;
	object_class->get_property = lrg_asset_meta_get_property;
	object_class->set_property = lrg_asset_meta_set_property;

	/**
	 * LrgAssetMeta:guid:
	 *
	 * The asset's stable identifier (%NULL if the file had none).
	 */
	properties[PROP_GUID] =
		g_param_spec_string ("guid", "Guid", "Stable asset identifier", NULL,
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * LrgAssetMeta:importer:
	 *
	 * The importer that reads the asset ("texture", "model", ...).
	 */
	properties[PROP_IMPORTER] =
		g_param_spec_string ("importer", "Importer", "Importer name", "generic",
		                     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgAssetMeta::setting-changed:
	 * @self: the sidecar
	 * @key: the setting that was stored or removed
	 *
	 * Emitted after a setting is stored or removed. The signal is
	 * detailed by the key, so "setting-changed::filter" only fires for
	 * the texture filter.
	 */
	signals[SIGNAL_SETTING_CHANGED] =
		g_signal_new ("setting-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
lrg_asset_meta_init (LrgAssetMeta *self)
{
	self->root = yaml_mapping_new ();
}

/* Returns the "settings" mapping, creating it when @create is set. */
static YamlMapping *
settings_map (LrgAssetMeta *self,
              gboolean      create)
{
	YamlNode *n = yaml_mapping_get_member (self->root, "settings");

	if (n != NULL && yaml_node_get_node_type (n) == YAML_NODE_MAPPING)
		return yaml_node_get_mapping (n);
	if (!create)
		return NULL;

	/* Missing (or not a mapping: replace the bogus value). */
	{
		g_autoptr(YamlMapping) map = yaml_mapping_new ();

		yaml_mapping_set_mapping_member (self->root, "settings", map);
	}
	return yaml_node_get_mapping (yaml_mapping_get_member (self->root, "settings"));
}

static void
mark_setting_changed (LrgAssetMeta *self,
                      const gchar  *key)
{
	self->dirty = TRUE;
	g_signal_emit (self, signals[SIGNAL_SETTING_CHANGED], g_quark_from_string (key), key);
}

/* ==========================================================================
 * Construction and I/O
 * ========================================================================== */

LrgAssetMeta *
lrg_asset_meta_new (void)
{
	LrgAssetMeta     *self = g_object_new (LRG_TYPE_ASSET_META, NULL);
	g_autofree gchar *uuid = g_uuid_string_random ();

	yaml_mapping_set_int_member (self->root, "version", LRG_ASSET_META_VERSION);
	yaml_mapping_set_string_member (self->root, "guid", uuid);
	yaml_mapping_set_string_member (self->root, "importer", "generic");
	self->dirty = TRUE;
	return self;
}

LrgAssetMeta *
lrg_asset_meta_new_for_asset (const gchar *asset_path)
{
	LrgAssetMeta *self;

	g_return_val_if_fail (asset_path != NULL, NULL);

	self = lrg_asset_meta_new ();
	yaml_mapping_set_string_member (self->root, "importer",
	                                lrg_asset_meta_importer_for_type (lrg_asset_meta_classify (asset_path)));
	return self;
}

/* Adopts the root of a parsed document (an empty document is an empty
 * sidecar; anything but a mapping is an error). */
static LrgAssetMeta *
from_parser (YamlParser   *parser,
             const gchar  *what,
             GError      **error)
{
	YamlNode     *root = yaml_parser_get_root (parser);
	LrgAssetMeta *self;

	if (root != NULL && yaml_node_get_node_type (root) != YAML_NODE_MAPPING &&
	    yaml_node_get_node_type (root) != YAML_NODE_NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s is not a YAML mapping", what);
		return NULL;
	}
	self = g_object_new (LRG_TYPE_ASSET_META, NULL);
	if (root != NULL && yaml_node_get_node_type (root) == YAML_NODE_MAPPING)
	{
		g_clear_pointer (&self->root, yaml_mapping_unref);
		self->root = yaml_mapping_ref (yaml_node_get_mapping (root));
	}
	self->dirty = FALSE;
	return self;
}

LrgAssetMeta *
lrg_asset_meta_new_from_file (const gchar  *path,
                              GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;
	g_autofree gchar     *data = NULL;
	gsize                 len = 0;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* g_file_get_contents maps ENOENT to G_FILE_ERROR_NOENT; report the
	 * documented G_IO_ERROR_NOT_FOUND instead so callers test one code. */
	if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No such file %s", path);
		return NULL;
	}
	if (!g_file_get_contents (path, &data, &len, error))
		return NULL;
	if (len == 0)
	{
		LrgAssetMeta *self = g_object_new (LRG_TYPE_ASSET_META, NULL);

		self->dirty = FALSE;
		return self;
	}
	parser = yaml_parser_new ();
	yaml_parser_set_capture_comments (parser, TRUE);
	if (!yaml_parser_load_from_data (parser, data, (gssize) len, error))
	{
		g_prefix_error (error, "%s: ", path);
		return NULL;
	}
	return from_parser (parser, path, error);
}

LrgAssetMeta *
lrg_asset_meta_new_from_data (const gchar  *data,
                              gssize        length,
                              GError      **error)
{
	g_autoptr(YamlParser) parser = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (length < 0)
		length = (gssize) strlen (data);
	if (length == 0)
	{
		LrgAssetMeta *self = g_object_new (LRG_TYPE_ASSET_META, NULL);

		self->dirty = FALSE;
		return self;
	}
	parser = yaml_parser_new ();
	yaml_parser_set_capture_comments (parser, TRUE);
	if (!yaml_parser_load_from_data (parser, data, length, error))
		return NULL;
	return from_parser (parser, "sidecar data", error);
}

LrgAssetMeta *
lrg_asset_meta_load_for_asset (const gchar  *asset_path,
                               GError      **error)
{
	g_autofree gchar *meta_path = NULL;

	g_return_val_if_fail (asset_path != NULL, NULL);

	meta_path = lrg_asset_meta_path_for_asset (asset_path);
	return lrg_asset_meta_new_from_file (meta_path, error);
}

LrgAssetMeta *
lrg_asset_meta_copy (LrgAssetMeta *self)
{
	LrgAssetMeta       *copy;
	g_autoptr(YamlNode) wrap = NULL;
	g_autoptr(YamlNode) dup = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);

	/* yaml_node_copy() is deep, so the copy shares nothing mutable. */
	wrap = yaml_node_new_mapping (self->root);
	dup = yaml_node_copy (wrap);
	copy = g_object_new (LRG_TYPE_ASSET_META, NULL);
	g_clear_pointer (&copy->root, yaml_mapping_unref);
	copy->root = yaml_mapping_ref (yaml_node_get_mapping (dup));
	copy->dirty = self->dirty;
	return copy;
}

gchar *
lrg_asset_meta_path_for_asset (const gchar *asset_path)
{
	g_return_val_if_fail (asset_path != NULL, NULL);
	return g_strconcat (asset_path, LRG_ASSET_META_SUFFIX, NULL);
}

gboolean
lrg_asset_meta_is_meta_path (const gchar *path)
{
	return path != NULL && g_str_has_suffix (path, LRG_ASSET_META_SUFFIX);
}

static gboolean
has_ext (const gchar        *lower,
         const gchar * const *exts)
{
	guint i;

	for (i = 0; exts[i] != NULL; i++)
		if (g_str_has_suffix (lower, exts[i]))
			return TRUE;

	return FALSE;
}

/*
 * lrg_asset_meta_classify:
 *
 * The extension table moved here from lrg-asset-database.c in 0.9.0 so the
 * runtime asset manager classifies files exactly like the editor. CAD
 * extensions are tested first (".ccad" would otherwise never match).
 */
LrgAssetType
lrg_asset_meta_classify (const gchar *path)
{
	g_autofree gchar *lower = NULL;

	static const gchar * const tex[]   = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".pep", NULL };
	static const gchar * const model[] = { ".gltf", ".glb", ".obj", ".fbx",
	                                       ".stl", ".step", ".stp", ".iges", ".igs",
	                                       ".3mf", NULL };
	static const gchar * const cad[]   = { ".cad", ".ccad", NULL };
	static const gchar * const audio[] = { ".wav", ".ogg", ".flac", ".mp3", NULL };
	static const gchar * const font[]  = { ".ttf", ".otf", NULL };
	static const gchar * const script[] = { ".lua", ".py", ".js", ".c", NULL };
	static const gchar * const tileset[] = { ".tileset", ".tsx", NULL };

	if (path == NULL)
		return LRG_ASSET_TYPE_UNKNOWN;

	lower = g_ascii_strdown (path, -1);

	if (has_ext (lower, cad))
		return LRG_ASSET_TYPE_CAD;
	if (has_ext (lower, tex))
		return LRG_ASSET_TYPE_TEXTURE;
	if (has_ext (lower, model))
		return LRG_ASSET_TYPE_MODEL;
	if (has_ext (lower, audio))
		return LRG_ASSET_TYPE_AUDIO;
	if (has_ext (lower, font))
		return LRG_ASSET_TYPE_FONT;
	if (g_str_has_suffix (lower, ".rlevel"))
		return LRG_ASSET_TYPE_LEVEL;
	if (g_str_has_suffix (lower, ".rprefab"))
		return LRG_ASSET_TYPE_PREFAB;
	if (has_ext (lower, tileset))
		return LRG_ASSET_TYPE_TILESET;
	if (g_str_has_suffix (lower, ".scene"))
		return LRG_ASSET_TYPE_SCENE;
	if (has_ext (lower, script))
		return LRG_ASSET_TYPE_SCRIPT;

	return LRG_ASSET_TYPE_UNKNOWN;
}

const gchar *
lrg_asset_meta_importer_for_type (LrgAssetType type)
{
	switch (type)
	{
	case LRG_ASSET_TYPE_TEXTURE: return "texture";
	case LRG_ASSET_TYPE_MODEL:   return "model";
	case LRG_ASSET_TYPE_AUDIO:   return "audio";
	case LRG_ASSET_TYPE_FONT:    return "font";
	case LRG_ASSET_TYPE_SCRIPT:  return "script";
	case LRG_ASSET_TYPE_LEVEL:   return "level";
	case LRG_ASSET_TYPE_PREFAB:  return "prefab";
	case LRG_ASSET_TYPE_TILESET: return "tileset";
	case LRG_ASSET_TYPE_SCENE:   return "scene";
	case LRG_ASSET_TYPE_CAD:     return "cad";
	case LRG_ASSET_TYPE_UNKNOWN:
	default:                     return "generic";
	}
}

gchar *
lrg_asset_meta_compute_legacy_guid (const gchar *relative_path)
{
	g_return_val_if_fail (relative_path != NULL, NULL);

	/* Exactly what LrgAssetDatabase used before sidecars (0.8.0). */
	return g_compute_checksum_for_string (G_CHECKSUM_SHA256, relative_path, -1);
}

gchar *
lrg_asset_meta_to_data (LrgAssetMeta  *self,
                        GError       **error)
{
	g_autoptr(YamlNode)      node = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);

	node = yaml_node_new_mapping (self->root);
	doc = yaml_document_new_with_root (node);
	gen = yaml_generator_new ();
	yaml_generator_set_emit_comments (gen, TRUE);
	yaml_generator_set_document (gen, doc);
	return yaml_generator_to_data (gen, NULL, error);
}

gboolean
lrg_asset_meta_save_to_file (LrgAssetMeta  *self,
                             const gchar   *path,
                             GError       **error)
{
	g_autofree gchar *data = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	/* Stamp the format version for files that lacked one. */
	if (!yaml_mapping_has_member (self->root, "version"))
		yaml_mapping_set_int_member (self->root, "version", LRG_ASSET_META_VERSION);

	data = lrg_asset_meta_to_data (self, error);
	if (data == NULL)
		return FALSE;
	/* g_file_set_contents writes a temp file and renames it: atomic. */
	if (!g_file_set_contents (path, data, -1, error))
		return FALSE;
	self->dirty = FALSE;
	return TRUE;
}

gboolean
lrg_asset_meta_is_dirty (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), FALSE);
	return self->dirty;
}

void
lrg_asset_meta_set_dirty (LrgAssetMeta *self,
                          gboolean      dirty)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	self->dirty = dirty;
}

/* ==========================================================================
 * guid / importer
 * ========================================================================== */

const gchar *
lrg_asset_meta_get_guid (LrgAssetMeta *self)
{
	const gchar *guid;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);

	guid = node_text (yaml_mapping_get_member (self->root, "guid"));
	return (guid != NULL && *guid != '\0') ? guid : NULL;
}

void
lrg_asset_meta_set_guid (LrgAssetMeta *self,
                         const gchar  *guid)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));

	if (g_strcmp0 (lrg_asset_meta_get_guid (self), guid) == 0)
		return;
	if (guid == NULL || *guid == '\0')
		yaml_mapping_remove_member (self->root, "guid");
	else
		yaml_mapping_set_string_member (self->root, "guid", guid);
	self->dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GUID]);
}

const gchar *
lrg_asset_meta_regenerate_guid (LrgAssetMeta *self)
{
	g_autofree gchar *uuid = g_uuid_string_random ();

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);

	lrg_asset_meta_set_guid (self, uuid);
	return lrg_asset_meta_get_guid (self);
}

const gchar *
lrg_asset_meta_get_importer (LrgAssetMeta *self)
{
	const gchar *importer;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);

	importer = node_text (yaml_mapping_get_member (self->root, "importer"));
	return (importer != NULL && *importer != '\0') ? importer : "generic";
}

void
lrg_asset_meta_set_importer (LrgAssetMeta *self,
                             const gchar  *importer)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));

	if (importer == NULL || *importer == '\0')
		importer = "generic";
	if (g_strcmp0 (node_text (yaml_mapping_get_member (self->root, "importer")), importer) == 0)
		return;
	yaml_mapping_set_string_member (self->root, "importer", importer);
	self->dirty = TRUE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IMPORTER]);
}

/* ==========================================================================
 * Settings by key
 * ========================================================================== */

/* The stored node of @key, or NULL. */
static YamlNode *
stored (LrgAssetMeta *self,
        const gchar  *key)
{
	YamlMapping *map = settings_map (self, FALSE);

	return (map != NULL) ? yaml_mapping_get_member (map, key) : NULL;
}

/* The effective text of @key: stored scalar text, else the default of a
 * known setting of the current importer, else NULL. */
static const gchar *
effective_text (LrgAssetMeta *self,
                const gchar  *key)
{
	const gchar       *text = node_text (stored (self, key));
	const SettingSpec *spec;

	if (text != NULL)
		return text;
	spec = find_spec (lrg_asset_meta_get_importer (self), key);
	return (spec != NULL) ? spec->def : NULL;
}

gboolean
lrg_asset_meta_has_setting (LrgAssetMeta *self,
                            const gchar  *key)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	return stored (self, key) != NULL;
}

gboolean
lrg_asset_meta_remove_setting (LrgAssetMeta *self,
                               const gchar  *key)
{
	YamlMapping *map;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);

	map = settings_map (self, FALSE);
	if (map == NULL || !yaml_mapping_remove_member (map, key))
		return FALSE;
	mark_setting_changed (self, key);
	return TRUE;
}

GStrv
lrg_asset_meta_get_setting_keys (LrgAssetMeta *self)
{
	g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
	YamlMapping            *map;
	guint                   i;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);

	map = settings_map (self, FALSE);
	for (i = 0; map != NULL && i < yaml_mapping_get_size (map); i++)
		g_strv_builder_add (b, yaml_mapping_get_key (map, i));
	return g_strv_builder_end (b);
}

gchar *
lrg_asset_meta_get_setting_string (LrgAssetMeta *self,
                                   const gchar  *key)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	return g_strdup (effective_text (self, key));
}

gint64
lrg_asset_meta_get_setting_int (LrgAssetMeta *self,
                                const gchar  *key,
                                gint64        fallback)
{
	const gchar *text;
	gint64       v;
	gdouble      d;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), fallback);
	g_return_val_if_fail (key != NULL, fallback);

	text = effective_text (self, key);
	if (text_to_int (text, &v))
		return v;
	/* "64.0" written by hand still reads as 64. */
	if (text_to_double (text, &d) && d >= (gdouble) G_MININT64 && d <= (gdouble) G_MAXINT64)
		return (gint64) d;
	return fallback;
}

gdouble
lrg_asset_meta_get_setting_double (LrgAssetMeta *self,
                                   const gchar  *key,
                                   gdouble       fallback)
{
	gdouble v;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), fallback);
	g_return_val_if_fail (key != NULL, fallback);

	return text_to_double (effective_text (self, key), &v) ? v : fallback;
}

gboolean
lrg_asset_meta_get_setting_boolean (LrgAssetMeta *self,
                                    const gchar  *key,
                                    gboolean      fallback)
{
	const gchar *text;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), fallback);
	g_return_val_if_fail (key != NULL, fallback);

	text = effective_text (self, key);
	if (text_is_true (text))
		return TRUE;
	if (text_is_false (text))
		return FALSE;
	return fallback;
}

/* Stores @node under @key unless an equal scalar is already there. */
static void
store_node (LrgAssetMeta *self,
            const gchar  *key,
            YamlNode     *node)
{
	YamlMapping *map = settings_map (self, TRUE);
	YamlNode    *old = yaml_mapping_get_member (map, key);

	if (old != NULL && node_text (old) != NULL && node_text (node) != NULL &&
	    g_str_equal (node_text (old), node_text (node)))
		return;
	yaml_mapping_set_member (map, key, node);
	mark_setting_changed (self, key);
}

void
lrg_asset_meta_set_setting_string (LrgAssetMeta *self,
                                   const gchar  *key,
                                   const gchar  *value)
{
	g_autoptr(YamlNode) node = NULL;

	g_return_if_fail (LRG_IS_ASSET_META (self));
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);

	node = yaml_node_new_string (value);
	store_node (self, key, node);
}

void
lrg_asset_meta_set_setting_int (LrgAssetMeta *self,
                                const gchar  *key,
                                gint64        value)
{
	g_autoptr(YamlNode) node = NULL;

	g_return_if_fail (LRG_IS_ASSET_META (self));
	g_return_if_fail (key != NULL);

	node = yaml_node_new_int (value);
	store_node (self, key, node);
}

void
lrg_asset_meta_set_setting_double (LrgAssetMeta *self,
                                   const gchar  *key,
                                   gdouble       value)
{
	g_autoptr(YamlNode) node = NULL;
	gchar               buf[G_ASCII_DTOSTR_BUF_SIZE];

	g_return_if_fail (LRG_IS_ASSET_META (self));
	g_return_if_fail (key != NULL);

	/* Shortest round-tripping text ("%g" in yaml-glib loses digits). */
	node = yaml_node_new_scalar (format_double (buf, value), YAML_SCALAR_STYLE_PLAIN);
	store_node (self, key, node);
}

void
lrg_asset_meta_set_setting_boolean (LrgAssetMeta *self,
                                    const gchar  *key,
                                    gboolean      value)
{
	g_autoptr(YamlNode) node = NULL;

	g_return_if_fail (LRG_IS_ASSET_META (self));
	g_return_if_fail (key != NULL);

	node = yaml_node_new_boolean (value);
	store_node (self, key, node);
}

/* ==========================================================================
 * GVariant access
 * ========================================================================== */

/* Types scalar text by its look: bool, int, double, else string. */
static GVariant *
variant_from_text (const gchar *text)
{
	gint64  i;
	gdouble d;

	if (g_ascii_strcasecmp (text, "true") == 0 || g_ascii_strcasecmp (text, "false") == 0)
		return g_variant_new_boolean (g_ascii_strcasecmp (text, "true") == 0);
	if (text_to_int (text, &i))
		return g_variant_new_int64 (i);
	if (text_to_double (text, &d))
		return g_variant_new_double (d);
	return g_variant_new_string (text);
}

/* Converts any stored node to a variant (a{sv} / av for containers;
 * YAML null becomes an empty "mv"). Returns a floating reference. */
static GVariant *
variant_from_node (YamlNode *node)
{
	switch (yaml_node_get_node_type (node))
	{
	case YAML_NODE_SCALAR:
		return variant_from_text (yaml_node_get_scalar (node) != NULL ? yaml_node_get_scalar (node) : "");
	case YAML_NODE_MAPPING:
		{
			YamlMapping    *map = yaml_node_get_mapping (node);
			GVariantBuilder b;
			guint           i;

			g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
			for (i = 0; i < yaml_mapping_get_size (map); i++)
				g_variant_builder_add (&b, "{sv}", yaml_mapping_get_key (map, i),
				                       variant_from_node (yaml_mapping_get_value (map, i)));
			return g_variant_builder_end (&b);
		}
	case YAML_NODE_SEQUENCE:
		{
			YamlSequence   *seq = yaml_node_get_sequence (node);
			GVariantBuilder b;
			guint           i;

			g_variant_builder_init (&b, G_VARIANT_TYPE ("av"));
			for (i = 0; i < yaml_sequence_get_length (seq); i++)
				g_variant_builder_add (&b, "v", variant_from_node (yaml_sequence_get_element (seq, i)));
			return g_variant_builder_end (&b);
		}
	case YAML_NODE_NULL:
	default:
		return g_variant_new_maybe (G_VARIANT_TYPE_VARIANT, NULL);
	}
}

/* Typed variant of known-setting text (falls back to the default when the
 * stored text does not parse). Floating. */
static GVariant *
variant_for_spec (const SettingSpec *spec,
                  const gchar       *text)
{
	gint64  i;
	gdouble d;

	switch (spec->kind)
	{
	case SPEC_BOOL:
		if (text_is_true (text))
			return g_variant_new_boolean (TRUE);
		if (text_is_false (text))
			return g_variant_new_boolean (FALSE);
		return g_variant_new_boolean (text_is_true (spec->def));
	case SPEC_INT:
		if (text_to_int (text, &i))
			return g_variant_new_int64 (i);
		if (text_to_double (text, &d))
			return g_variant_new_int64 ((gint64) d);
		text_to_int (spec->def, &i);
		return g_variant_new_int64 (i);
	case SPEC_DOUBLE:
		if (!text_to_double (text, &d))
			text_to_double (spec->def, &d);
		return g_variant_new_double (d);
	case SPEC_STRING:
	case SPEC_CHOICE:
	default:
		return g_variant_new_string (text != NULL ? text : spec->def);
	}
}

GVariant *
lrg_asset_meta_get_setting_variant (LrgAssetMeta *self,
                                    const gchar  *key)
{
	const SettingSpec *spec;
	YamlNode          *node;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);
	g_return_val_if_fail (key != NULL, NULL);

	spec = find_spec (lrg_asset_meta_get_importer (self), key);
	node = stored (self, key);
	if (spec != NULL && spec->kind == SPEC_STRING && node != NULL &&
	    yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
	{
		g_autofree gchar *joined = sequence_text (node);

		return g_variant_ref_sink (g_variant_new_string (joined));
	}
	if (spec != NULL)
		return g_variant_ref_sink (variant_for_spec (spec, node_text (node)));
	if (node == NULL)
		return NULL;
	return g_variant_ref_sink (variant_from_node (node));
}

/* Converts a variant to a YAML node (scalars, a{sv}/a{ss}, arrays,
 * boxed "v"). Returns NULL for unsupported types. */
static YamlNode *
node_from_variant (GVariant *value)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	if (g_variant_is_of_type (value, G_VARIANT_TYPE_VARIANT))
	{
		g_autoptr(GVariant) inner = g_variant_get_variant (value);

		return node_from_variant (inner);
	}
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
		return yaml_node_new_boolean (g_variant_get_boolean (value));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
		return yaml_node_new_string (g_variant_get_string (value, NULL));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT64))
		return yaml_node_new_int (g_variant_get_int64 (value));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
		return yaml_node_new_int (g_variant_get_int32 (value));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
		return yaml_node_new_int (g_variant_get_uint32 (value));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT16))
		return yaml_node_new_int (g_variant_get_int16 (value));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT16))
		return yaml_node_new_int (g_variant_get_uint16 (value));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTE))
		return yaml_node_new_int (g_variant_get_byte (value));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64) && g_variant_get_uint64 (value) <= (guint64) G_MAXINT64)
		return yaml_node_new_int ((gint64) g_variant_get_uint64 (value));
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE))
	{
		return yaml_node_new_scalar (format_double (buf, g_variant_get_double (value)), YAML_SCALAR_STYLE_PLAIN);
	}
	if (g_variant_is_of_type (value, G_VARIANT_TYPE ("a{s*}")))
	{
		g_autoptr(YamlMapping) map = yaml_mapping_new ();
		GVariantIter           iter;
		GVariant              *child;

		g_variant_iter_init (&iter, value);
		while ((child = g_variant_iter_next_value (&iter)) != NULL)
		{
			g_autoptr(GVariant) k = g_variant_get_child_value (child, 0);
			g_autoptr(GVariant) v = g_variant_get_child_value (child, 1);
			g_autoptr(YamlNode) n = node_from_variant (v);

			g_variant_unref (child);
			if (n == NULL)
				return NULL;
			yaml_mapping_set_member (map, g_variant_get_string (k, NULL), n);
		}
		return yaml_node_new_mapping (map);
	}
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_ARRAY))
	{
		g_autoptr(YamlSequence) seq = yaml_sequence_new ();
		GVariantIter            iter;
		GVariant               *child;

		g_variant_iter_init (&iter, value);
		while ((child = g_variant_iter_next_value (&iter)) != NULL)
		{
			g_autoptr(YamlNode) n = node_from_variant (child);

			g_variant_unref (child);
			if (n == NULL)
				return NULL;
			yaml_sequence_add_element (seq, n);
		}
		return yaml_node_new_sequence (seq);
	}
	return NULL;
}

/* Reads any integral or floating variant as a double; strings that look
 * like numbers are accepted too (JSON clients sometimes quote). */
static gboolean
variant_number (GVariant *value,
                gdouble  *out,
                gboolean *integral)
{
	gint64 i;

	*integral = TRUE;
	if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT64))
		*out = (gdouble) g_variant_get_int64 (value);
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT32))
		*out = g_variant_get_int32 (value);
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT32))
		*out = g_variant_get_uint32 (value);
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT64))
		*out = (gdouble) g_variant_get_uint64 (value);
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_INT16))
		*out = g_variant_get_int16 (value);
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_UINT16))
		*out = g_variant_get_uint16 (value);
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_BYTE))
		*out = g_variant_get_byte (value);
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_DOUBLE))
	{
		*out = g_variant_get_double (value);
		*integral = (*out == (gdouble) (gint64) *out);
	}
	else if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
	{
		const gchar *s = g_variant_get_string (value, NULL);

		if (text_to_int (s, &i))
			*out = (gdouble) i;
		else if (text_to_double (s, out))
			*integral = (*out == (gdouble) (gint64) *out);
		else
			return FALSE;
	}
	else
		return FALSE;
	return TRUE;
}

/* Validates @value against @spec and stores it. */
static gboolean
set_known (LrgAssetMeta       *self,
           const SettingSpec  *spec,
           GVariant           *value,
           GError            **error)
{
	gdouble  d;
	gboolean integral;

	switch (spec->kind)
	{
	case SPEC_BOOL:
		if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
		{
			lrg_asset_meta_set_setting_boolean (self, spec->key, g_variant_get_boolean (value));
			return TRUE;
		}
		if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING) &&
		    (text_is_true (g_variant_get_string (value, NULL)) || text_is_false (g_variant_get_string (value, NULL))))
		{
			lrg_asset_meta_set_setting_boolean (self, spec->key, text_is_true (g_variant_get_string (value, NULL)));
			return TRUE;
		}
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Setting '%s' expects a boolean", spec->key);
		return FALSE;

	case SPEC_INT:
	case SPEC_DOUBLE:
		if (!variant_number (value, &d, &integral) || (spec->kind == SPEC_INT && !integral))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Setting '%s' expects %s", spec->key,
			             spec->kind == SPEC_INT ? "an integer" : "a number");
			return FALSE;
		}
		if (d < spec->min || d > spec->max)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Setting '%s' must be within %g .. %g",
			             spec->key, spec->min, spec->max);
			return FALSE;
		}
		if (spec->kind == SPEC_INT)
			lrg_asset_meta_set_setting_int (self, spec->key, (gint64) d);
		else
			lrg_asset_meta_set_setting_double (self, spec->key, d);
		return TRUE;

	case SPEC_CHOICE:
		if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
		{
			g_autoptr(GEnumClass) klass = g_type_class_ref (spec->choices ());
			const gchar          *s = g_variant_get_string (value, NULL);

			if (g_enum_get_value_by_nick (klass, s) != NULL)
			{
				lrg_asset_meta_set_setting_string (self, spec->key, s);
				return TRUE;
			}
		}
		{
			g_auto(GStrv)     choices = lrg_asset_meta_get_setting_choices (spec->importer, spec->key);
			g_autofree gchar *list = g_strjoinv ("|", choices);

			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Setting '%s' expects one of %s",
			             spec->key, list);
		}
		return FALSE;

	case SPEC_STRING:
	default:
		if (!g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Setting '%s' expects a string", spec->key);
			return FALSE;
		}
		if (g_str_equal (spec->key, "codepoints"))
		{
			g_autoptr(GArray) cps = lrg_asset_meta_parse_codepoints (g_variant_get_string (value, NULL), error);

			if (cps == NULL)
				return FALSE;
		}
		if (g_str_equal (spec->key, "lod-ratios") || g_str_equal (spec->key, "lod-screen-sizes"))
		{
			g_autoptr(GArray) list = lrg_asset_meta_parse_float_list (g_variant_get_string (value, NULL), error);

			if (list == NULL)
				return FALSE;
		}
		lrg_asset_meta_set_setting_string (self, spec->key, g_variant_get_string (value, NULL));
		return TRUE;
	}
}

gboolean
lrg_asset_meta_set_setting_variant (LrgAssetMeta  *self,
                                    const gchar   *key,
                                    GVariant      *value,
                                    GError       **error)
{
	const SettingSpec  *spec;
	g_autoptr(GVariant) held = NULL;
	g_autoptr(GVariant) unboxed = NULL;
	g_autoptr(YamlNode) node = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* Take ownership of a floating @value right away so every return
	 * path below releases it. */
	if (value != NULL)
		held = g_variant_ref_sink (value);

	if (*key == '\0')
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Empty setting key");
		return FALSE;
	}

	/* NULL (or an empty maybe) resets to the default. */
	if (held == NULL || (g_variant_is_of_type (held, G_VARIANT_TYPE_MAYBE) && g_variant_n_children (held) == 0))
	{
		lrg_asset_meta_remove_setting (self, key);
		return TRUE;
	}
	/* Unbox "v" so callers can pass values straight out of an a{sv}. */
	unboxed = g_variant_is_of_type (held, G_VARIANT_TYPE_VARIANT) ? g_variant_get_variant (held)
	                                                                : g_variant_ref (held);

	spec = find_spec (lrg_asset_meta_get_importer (self), key);
	if (spec != NULL)
		return set_known (self, spec, unboxed, error);

	node = node_from_variant (unboxed);
	if (node == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unsupported value type '%s' for '%s'",
		             g_variant_get_type_string (unboxed), key);
		return FALSE;
	}
	if (yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
		store_node (self, key, node);
	else
	{
		yaml_mapping_set_member (settings_map (self, TRUE), key, node);
		mark_setting_changed (self, key);
	}
	return TRUE;
}

static gint
cmp_str (gconstpointer a,
         gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

GVariant *
lrg_asset_meta_get_settings_variant (LrgAssetMeta *self,
                                     gboolean      with_defaults)
{
	g_autoptr(GPtrArray) keys = g_ptr_array_new_with_free_func (g_free);
	GVariantBuilder      b;
	YamlMapping         *map;
	const gchar         *importer;
	guint                i;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);

	/* Stored keys plus (optionally) the importer's known keys, sorted so
	 * the result (and the hash built from it) is order independent. */
	importer = lrg_asset_meta_get_importer (self);
	map = settings_map (self, FALSE);
	for (i = 0; map != NULL && i < yaml_mapping_get_size (map); i++)
		g_ptr_array_add (keys, g_strdup (yaml_mapping_get_key (map, i)));
	for (i = 0; with_defaults && i < G_N_ELEMENTS (setting_specs); i++)
		if (g_str_equal (setting_specs[i].importer, importer) &&
		    (map == NULL || !yaml_mapping_has_member (map, setting_specs[i].key)))
			g_ptr_array_add (keys, g_strdup (setting_specs[i].key));
	g_ptr_array_sort (keys, cmp_str);

	g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
	for (i = 0; i < keys->len; i++)
	{
		const gchar        *key = g_ptr_array_index (keys, i);
		g_autoptr(GVariant) v = lrg_asset_meta_get_setting_variant (self, key);

		if (v != NULL)
			g_variant_builder_add (&b, "{sv}", key, v);
	}
	return g_variant_ref_sink (g_variant_builder_end (&b));
}

GVariant *
lrg_asset_meta_to_variant (LrgAssetMeta *self)
{
	GVariantBuilder     b;
	g_autoptr(GVariant) settings = NULL;
	const gchar        *guid;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);

	guid = lrg_asset_meta_get_guid (self);
	settings = lrg_asset_meta_get_settings_variant (self, TRUE);
	g_variant_builder_init (&b, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&b, "{sv}", "guid", g_variant_new_string (guid != NULL ? guid : ""));
	g_variant_builder_add (&b, "{sv}", "importer", g_variant_new_string (lrg_asset_meta_get_importer (self)));
	g_variant_builder_add (&b, "{sv}", "settings", settings);
	return g_variant_ref_sink (g_variant_builder_end (&b));
}

gchar *
lrg_asset_meta_get_settings_hash (LrgAssetMeta *self)
{
	g_autoptr(GVariant) settings = NULL;
	g_autofree gchar   *text = NULL;
	g_autofree gchar   *salted = NULL;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);

	/* The printed, sorted a{sv} is a canonical form; the importer is part
	 * of it because the same keys mean different things per importer. */
	settings = lrg_asset_meta_get_settings_variant (self, TRUE);
	text = g_variant_print (settings, TRUE);
	salted = g_strconcat (lrg_asset_meta_get_importer (self), "\n", text, NULL);
	return g_compute_checksum_for_string (G_CHECKSUM_SHA1, salted, -1);
}

GStrv
lrg_asset_meta_list_known_settings (const gchar *importer)
{
	g_autoptr(GStrvBuilder) b = g_strv_builder_new ();
	guint                   i;

	for (i = 0; importer != NULL && i < G_N_ELEMENTS (setting_specs); i++)
		if (g_str_equal (setting_specs[i].importer, importer))
			g_strv_builder_add (b, setting_specs[i].key);
	return g_strv_builder_end (b);
}

GStrv
lrg_asset_meta_get_setting_choices (const gchar *importer,
                                    const gchar *key)
{
	const SettingSpec      *spec = find_spec (importer, key);
	g_autoptr(GStrvBuilder) b = NULL;
	g_autoptr(GEnumClass)   klass = NULL;
	guint                   i;

	if (spec == NULL || spec->kind != SPEC_CHOICE)
		return NULL;
	b = g_strv_builder_new ();
	klass = g_type_class_ref (spec->choices ());
	for (i = 0; i < klass->n_values; i++)
		g_strv_builder_add (b, klass->values[i].value_nick);
	return g_strv_builder_end (b);
}

GVariant *
lrg_asset_meta_get_default_setting (const gchar *importer,
                                    const gchar *key)
{
	const SettingSpec *spec = find_spec (importer, key);

	if (spec == NULL)
		return NULL;
	return g_variant_ref_sink (variant_for_spec (spec, spec->def));
}

/* ==========================================================================
 * Typed accessors
 * ========================================================================== */

/* Reads a choice setting as its enum value (default on garbage). */
static gint
get_choice (LrgAssetMeta *self,
            const gchar  *importer,
            const gchar  *key,
            GType         enum_type)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (enum_type);
	const SettingSpec    *spec = find_spec (importer, key);
	const gchar          *text = node_text (stored (self, key));
	GEnumValue           *v = (text != NULL) ? g_enum_get_value_by_nick (klass, text) : NULL;

	if (v == NULL)
		v = g_enum_get_value_by_nick (klass, spec->def);
	return v->value;
}

static void
set_choice (LrgAssetMeta *self,
            const gchar  *key,
            GType         enum_type,
            gint          value)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (enum_type);
	GEnumValue           *v = g_enum_get_value (klass, value);

	g_return_if_fail (v != NULL);
	lrg_asset_meta_set_setting_string (self, key, v->value_nick);
}

/* Typed getters read the spec default even when the importer differs, so
 * a "generic" sidecar still answers sensibly. */
static gboolean
get_bool_spec (LrgAssetMeta *self,
               const gchar  *importer,
               const gchar  *key)
{
	const SettingSpec *spec = find_spec (importer, key);
	const gchar       *text = node_text (stored (self, key));

	if (text_is_true (text))
		return TRUE;
	if (text_is_false (text))
		return FALSE;
	return text_is_true (spec->def);
}

static gdouble
get_number_spec (LrgAssetMeta *self,
                 const gchar  *importer,
                 const gchar  *key)
{
	const SettingSpec *spec = find_spec (importer, key);
	gdouble            d;

	if (!text_to_double (node_text (stored (self, key)), &d))
		text_to_double (spec->def, &d);
	return CLAMP (d, spec->min, spec->max);
}

LrgAssetTextureFilter
lrg_asset_meta_get_texture_filter (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), LRG_ASSET_TEXTURE_FILTER_BILINEAR);
	return (LrgAssetTextureFilter) get_choice (self, "texture", "filter", LRG_TYPE_ASSET_TEXTURE_FILTER);
}

void
lrg_asset_meta_set_texture_filter (LrgAssetMeta          *self,
                                   LrgAssetTextureFilter  filter)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	set_choice (self, "filter", LRG_TYPE_ASSET_TEXTURE_FILTER, (gint) filter);
}

gboolean
lrg_asset_meta_get_texture_mipmaps (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), FALSE);
	return get_bool_spec (self, "texture", "mipmaps");
}

void
lrg_asset_meta_set_texture_mipmaps (LrgAssetMeta *self,
                                    gboolean      mipmaps)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_boolean (self, "mipmaps", mipmaps);
}

LrgAssetTextureWrap
lrg_asset_meta_get_texture_wrap (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), LRG_ASSET_TEXTURE_WRAP_REPEAT);
	return (LrgAssetTextureWrap) get_choice (self, "texture", "wrap", LRG_TYPE_ASSET_TEXTURE_WRAP);
}

void
lrg_asset_meta_set_texture_wrap (LrgAssetMeta        *self,
                                 LrgAssetTextureWrap  wrap)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	set_choice (self, "wrap", LRG_TYPE_ASSET_TEXTURE_WRAP, (gint) wrap);
}

gboolean
lrg_asset_meta_get_texture_srgb (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), TRUE);
	return get_bool_spec (self, "texture", "srgb");
}

void
lrg_asset_meta_set_texture_srgb (LrgAssetMeta *self,
                                 gboolean      srgb)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_boolean (self, "srgb", srgb);
}

guint
lrg_asset_meta_get_texture_max_size (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), 0);
	return (guint) get_number_spec (self, "texture", "max-size");
}

void
lrg_asset_meta_set_texture_max_size (LrgAssetMeta *self,
                                     guint         max_size)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_int (self, "max-size", (gint64) MIN (max_size, 16384u));
}

gdouble
lrg_asset_meta_get_model_scale (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), 1.0);
	return get_number_spec (self, "model", "scale");
}

void
lrg_asset_meta_set_model_scale (LrgAssetMeta *self,
                                gdouble       scale)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	g_return_if_fail (scale > 0.0);
	lrg_asset_meta_set_setting_double (self, "scale", scale);
}

LrgAssetUpAxis
lrg_asset_meta_get_model_up_axis (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), LRG_ASSET_UP_AXIS_Y);
	return (LrgAssetUpAxis) get_choice (self, "model", "up-axis", LRG_TYPE_ASSET_UP_AXIS);
}

void
lrg_asset_meta_set_model_up_axis (LrgAssetMeta   *self,
                                  LrgAssetUpAxis  axis)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	set_choice (self, "up-axis", LRG_TYPE_ASSET_UP_AXIS, (gint) axis);
}

gboolean
lrg_asset_meta_get_model_generate_normals (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), FALSE);
	return get_bool_spec (self, "model", "generate-normals");
}

void
lrg_asset_meta_set_model_generate_normals (LrgAssetMeta *self,
                                           gboolean      generate)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_boolean (self, "generate-normals", generate);
}

gboolean
lrg_asset_meta_get_model_import_animations (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), TRUE);
	return get_bool_spec (self, "model", "import-animations");
}

void
lrg_asset_meta_set_model_import_animations (LrgAssetMeta *self,
                                            gboolean      import)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_boolean (self, "import-animations", import);
}

/* The effective text of a list setting: a stored scalar, a stored
 * sequence joined with ", ", else the default. */
static gchar *
list_setting_text (LrgAssetMeta *self,
                   const gchar  *key)
{
	YamlNode *node = stored (self, key);

	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SEQUENCE)
		return sequence_text (node);
	return g_strdup (effective_text (self, key) != NULL ? effective_text (self, key) : "");
}

guint
lrg_asset_meta_get_model_lod_levels (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), 1);
	return (guint) CLAMP (get_number_spec (self, "model", "lod-levels"), 1.0, 8.0);
}

void
lrg_asset_meta_set_model_lod_levels (LrgAssetMeta *self,
                                     guint         levels)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_int (self, "lod-levels", (gint64) CLAMP (levels, 1u, 8u));
}

gchar *
lrg_asset_meta_get_model_lod_ratios (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);
	return list_setting_text (self, "lod-ratios");
}

void
lrg_asset_meta_set_model_lod_ratios (LrgAssetMeta *self,
                                     const gchar  *ratios)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	g_return_if_fail (ratios != NULL);
	lrg_asset_meta_set_setting_string (self, "lod-ratios", ratios);
}

gchar *
lrg_asset_meta_get_model_lod_screen_sizes (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);
	return list_setting_text (self, "lod-screen-sizes");
}

void
lrg_asset_meta_set_model_lod_screen_sizes (LrgAssetMeta *self,
                                           const gchar  *sizes)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	g_return_if_fail (sizes != NULL);
	lrg_asset_meta_set_setting_string (self, "lod-screen-sizes", sizes);
}

gdouble
lrg_asset_meta_get_model_lod_max_error (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), 0.05);
	return get_number_spec (self, "model", "lod-max-error");
}

void
lrg_asset_meta_set_model_lod_max_error (LrgAssetMeta *self,
                                        gdouble       max_error)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_double (self, "lod-max-error", CLAMP (max_error, 0.0, 1.0));
}

gboolean
lrg_asset_meta_get_model_lod_lock_border (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), TRUE);
	return get_bool_spec (self, "model", "lod-lock-border");
}

void
lrg_asset_meta_set_model_lod_lock_border (LrgAssetMeta *self,
                                          gboolean      lock)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_boolean (self, "lod-lock-border", lock);
}

gdouble
lrg_asset_meta_get_model_lod_hysteresis (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), 0.1);
	return get_number_spec (self, "model", "lod-hysteresis");
}

void
lrg_asset_meta_set_model_lod_hysteresis (LrgAssetMeta *self,
                                         gdouble       hysteresis)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_double (self, "lod-hysteresis", CLAMP (hysteresis, 0.0, 0.9));
}

gdouble
lrg_asset_meta_get_model_lod_fade_duration (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), 0.25);
	return get_number_spec (self, "model", "lod-fade-duration");
}

void
lrg_asset_meta_set_model_lod_fade_duration (LrgAssetMeta *self,
                                            gdouble       seconds)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_double (self, "lod-fade-duration", CLAMP (seconds, 0.0, 10.0));
}

GArray *
lrg_asset_meta_parse_float_list (const gchar  *text,
                                 GError      **error)
{
	g_autoptr(GArray) out = NULL;
	g_autofree gchar *copy = NULL;
	g_auto(GStrv)     parts = NULL;
	guint             i;

	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* Accept "0.5, 0.25", "0.5 0.25", "[0.5, 0.25]" alike. */
	copy = g_strdup (text);
	g_strdelimit (copy, "[]", ' ');
	g_strdelimit (copy, " \t;", ',');
	out = g_array_new (FALSE, FALSE, sizeof (gfloat));
	parts = g_strsplit (copy, ",", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		gchar  *item = g_strstrip (parts[i]);
		gdouble v;
		gfloat  f;

		if (*item == '\0')
			continue;
		if (!text_to_double (item, &v))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Bad number '%s' in list", item);
			return NULL;
		}
		f = (gfloat) v;
		g_array_append_val (out, f);
	}
	return g_steal_pointer (&out);
}

LrgAssetAudioMode
lrg_asset_meta_get_audio_mode (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), LRG_ASSET_AUDIO_MODE_SAMPLE);
	return (LrgAssetAudioMode) get_choice (self, "audio", "mode", LRG_TYPE_ASSET_AUDIO_MODE);
}

void
lrg_asset_meta_set_audio_mode (LrgAssetMeta      *self,
                               LrgAssetAudioMode  mode)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	set_choice (self, "mode", LRG_TYPE_ASSET_AUDIO_MODE, (gint) mode);
}

gboolean
lrg_asset_meta_get_audio_loop (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), FALSE);
	return get_bool_spec (self, "audio", "loop");
}

void
lrg_asset_meta_set_audio_loop (LrgAssetMeta *self,
                               gboolean      loop)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_boolean (self, "loop", loop);
}

gdouble
lrg_asset_meta_get_audio_volume (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), 1.0);
	return get_number_spec (self, "audio", "volume");
}

void
lrg_asset_meta_set_audio_volume (LrgAssetMeta *self,
                                 gdouble       volume)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_double (self, "volume", CLAMP (volume, 0.0, 4.0));
}

guint
lrg_asset_meta_get_font_size (LrgAssetMeta *self)
{
	g_return_val_if_fail (LRG_IS_ASSET_META (self), 32);
	return (guint) get_number_spec (self, "font", "size");
}

void
lrg_asset_meta_set_font_size (LrgAssetMeta *self,
                              guint         size)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	lrg_asset_meta_set_setting_int (self, "size", (gint64) CLAMP (size, 1u, 1024u));
}

gchar *
lrg_asset_meta_get_font_codepoints (LrgAssetMeta *self)
{
	const gchar *text;

	g_return_val_if_fail (LRG_IS_ASSET_META (self), NULL);

	text = node_text (stored (self, "codepoints"));
	return g_strdup (text != NULL ? text : find_spec ("font", "codepoints")->def);
}

void
lrg_asset_meta_set_font_codepoints (LrgAssetMeta *self,
                                    const gchar  *ranges)
{
	g_return_if_fail (LRG_IS_ASSET_META (self));
	g_return_if_fail (ranges != NULL);
	lrg_asset_meta_set_setting_string (self, "codepoints", ranges);
}

/* Parses one code point: decimal or 0x/U+ hex. */
static gboolean
parse_codepoint (const gchar *text,
                 guint32     *out)
{
	g_autofree gchar *t = g_strstrip (g_strdup (text));
	gchar            *end = NULL;
	guint64           v;
	guint             base = 10;
	const gchar      *digits = t;

	if (g_str_has_prefix (t, "0x") || g_str_has_prefix (t, "0X") || g_str_has_prefix (t, "U+") ||
	    g_str_has_prefix (t, "u+"))
	{
		base = 16;
		digits = t + 2;
	}
	if (*digits == '\0')
		return FALSE;
	errno = 0;
	v = g_ascii_strtoull (digits, &end, base);
	if (errno != 0 || end == NULL || *end != '\0' || v > 0x10FFFF)
		return FALSE;
	*out = (guint32) v;
	return TRUE;
}

static gint
cmp_u32 (gconstpointer a,
         gconstpointer b)
{
	guint32 x = *(const guint32 *) a;
	guint32 y = *(const guint32 *) b;

	return (x > y) - (x < y);
}

GArray *
lrg_asset_meta_parse_codepoints (const gchar  *ranges,
                                 GError      **error)
{
	g_autoptr(GArray) out = NULL;
	g_auto(GStrv)     parts = NULL;
	guint             i, w;

	g_return_val_if_fail (ranges != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	out = g_array_new (FALSE, FALSE, sizeof (guint32));
	parts = g_strsplit (ranges, ",", -1);
	for (i = 0; parts[i] != NULL; i++)
	{
		gchar   *item = g_strstrip (parts[i]);
		gchar   *dash;
		guint32  lo, hi, c;

		if (*item == '\0')
			continue;
		/* "lo-hi" (the dash after the first char so "-" alone fails). */
		dash = strchr (item + 1, '-');
		if (dash != NULL)
		{
			*dash = '\0';
			if (!parse_codepoint (item, &lo) || !parse_codepoint (dash + 1, &hi) || hi < lo)
			{
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Bad code point range '%s-%s'", item,
				             dash + 1);
				return NULL;
			}
		}
		else if (parse_codepoint (item, &lo))
			hi = lo;
		else
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Bad code point '%s'", item);
			return NULL;
		}
		for (c = lo; c <= hi; c++)
			g_array_append_val (out, c);
	}

	/* Sort and drop duplicates in place. */
	g_array_sort (out, cmp_u32);
	for (i = 0, w = 0; i < out->len; i++)
		if (w == 0 || g_array_index (out, guint32, i) != g_array_index (out, guint32, w - 1))
			g_array_index (out, guint32, w++) = g_array_index (out, guint32, i);
	g_array_set_size (out, w);
	return g_steal_pointer (&out);
}
