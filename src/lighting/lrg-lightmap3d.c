/* lrg-lightmap3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightmap3D: HDR atlas pages plus per-object (GUID-keyed) uv2
 * scale / offset entries, the `.rlightmap` index format and the GPU
 * upload of the pages.  See lrg-lightmap3d.h and
 * docs/modules/lighting/lightmap3d.org.
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <raylib.h>
#include <rlgl.h>
#include <yaml-glib.h>

#include "lrg-lightmap3d.h"
#include "lrg-baked-glsl-private.h"
#include "../graphics/lrg-light-set-private.h"

/*
 * LightmapEntry:
 * One object's placement in the atlas.
 */
typedef struct
{
	guint  page;
	gfloat scale_offset[4];
} LightmapEntry;

/*
 * LightmapPage:
 * One atlas page: the image, its GL texture (0 = none) and whether the
 * texture is older than the image.
 */
typedef struct
{
	LrgHdrImage *image;
	guint        texture;
	gboolean     dirty;
} LightmapPage;

struct _LrgLightmap3D
{
	GObject     parent_instance;

	GPtrArray  *pages;     /* LightmapPage * */
	GHashTable *entries;   /* gchar * guid -> LightmapEntry * */
	GHashTable *settings;  /* gchar * key -> gdouble * */
	gfloat      intensity;
	gchar      *path;
};

G_DEFINE_FINAL_TYPE (LrgLightmap3D, lrg_lightmap3d, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_INTENSITY,
	PROP_N_PAGES,
	PROP_N_ENTRIES,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
	SIGNAL_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Error domain and enum type
 * ========================================================================== */

G_DEFINE_QUARK (lrg-lightmap3d-error-quark, lrg_lightmap3d_error)

GType
lrg_lightmap3d_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_LIGHTMAP3D_ERROR_FAILED, "LRG_LIGHTMAP3D_ERROR_FAILED", "failed" },
			{ LRG_LIGHTMAP3D_ERROR_IO, "LRG_LIGHTMAP3D_ERROR_IO", "io" },
			{ LRG_LIGHTMAP3D_ERROR_FORMAT, "LRG_LIGHTMAP3D_ERROR_FORMAT", "format" },
			{ LRG_LIGHTMAP3D_ERROR_VERSION, "LRG_LIGHTMAP3D_ERROR_VERSION", "version" },
			{ LRG_LIGHTMAP3D_ERROR_GPU, "LRG_LIGHTMAP3D_ERROR_GPU", "gpu" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgLightmap3DError"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType)type_id;
}

GType
lrg_light_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_LIGHT_MODE_REALTIME, "LRG_LIGHT_MODE_REALTIME", "realtime" },
			{ LRG_LIGHT_MODE_MIXED, "LRG_LIGHT_MODE_MIXED", "mixed" },
			{ LRG_LIGHT_MODE_BAKED, "LRG_LIGHT_MODE_BAKED", "baked" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgLightMode"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType)type_id;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* frees one page and its texture (the texture only with a context) */
static void
page_free (gpointer data)
{
	LightmapPage *p = data;

	if (p == NULL)
		return;
	if (p->texture != 0 && IsWindowReady ())
		rlUnloadTexture (p->texture);
	g_clear_object (&p->image);
	g_free (p);
}

static void
emit_changed (LrgLightmap3D *self)
{
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

/*
 * parse_number:
 * A scalar node as a double: numbers, and true / false as 1 / 0.
 * Returns FALSE for anything else.
 */
static gboolean
parse_number (YamlNode *node,
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
	return end != s && end != NULL && *end == '\0';
}

/* a double as a YAML scalar node with up to 9 significant digits */
static YamlNode *
number_node (gdouble value)
{
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	return yaml_node_new_scalar (g_ascii_formatd (buf, sizeof buf, "%.9g", value), YAML_SCALAR_STYLE_PLAIN);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_lightmap3d_finalize (GObject *object)
{
	LrgLightmap3D *self = LRG_LIGHTMAP3D (object);

	g_ptr_array_unref (self->pages);
	g_hash_table_unref (self->entries);
	g_hash_table_unref (self->settings);
	g_free (self->path);

	G_OBJECT_CLASS (lrg_lightmap3d_parent_class)->finalize (object);
}

static void
lrg_lightmap3d_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgLightmap3D *self = LRG_LIGHTMAP3D (object);

	switch (prop_id)
	{
	case PROP_INTENSITY:
		g_value_set_float (value, self->intensity);
		break;
	case PROP_N_PAGES:
		g_value_set_uint (value, self->pages->len);
		break;
	case PROP_N_ENTRIES:
		g_value_set_uint (value, g_hash_table_size (self->entries));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_lightmap3d_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
	LrgLightmap3D *self = LRG_LIGHTMAP3D (object);

	switch (prop_id)
	{
	case PROP_INTENSITY:
		lrg_lightmap3d_set_intensity (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
lrg_lightmap3d_class_init (LrgLightmap3DClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_lightmap3d_finalize;
	object_class->get_property = lrg_lightmap3d_get_property;
	object_class->set_property = lrg_lightmap3d_set_property;

	/**
	 * LrgLightmap3D:intensity:
	 *
	 * Multiplier on every texel (CPU samples and the shader).
	 */
	properties[PROP_INTENSITY] =
		g_param_spec_float ("intensity", "Intensity", "Multiplier on every texel",
		                    0.0f, G_MAXFLOAT, 1.0f,
		                    G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgLightmap3D:n-pages:
	 *
	 * The number of atlas pages.
	 */
	properties[PROP_N_PAGES] =
		g_param_spec_uint ("n-pages", "Pages", "Number of atlas pages",
		                   0, G_MAXUINT, 0,
		                   G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgLightmap3D:n-entries:
	 *
	 * The number of lightmapped objects.
	 */
	properties[PROP_N_ENTRIES] =
		g_param_spec_uint ("n-entries", "Entries", "Number of lightmapped objects",
		                   0, G_MAXUINT, 0,
		                   G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgLightmap3D::changed:
	 * @self: the lightmap
	 *
	 * Emitted after pages, entries or settings changed (a bake finished,
	 * a page was replaced, the lightmap was cleared).  Renderers and the
	 * editor refresh from it.
	 */
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
lrg_lightmap3d_init (LrgLightmap3D *self)
{
	self->pages = g_ptr_array_new_with_free_func (page_free);
	self->entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->intensity = 1.0f;
}

LrgLightmap3D *
lrg_lightmap3d_new (void)
{
	return g_object_new (LRG_TYPE_LIGHTMAP3D, NULL);
}

const gchar *
lrg_lightmap3d_get_path (LrgLightmap3D *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), NULL);
	return self->path;
}

void
lrg_lightmap3d_clear (LrgLightmap3D *self)
{
	g_return_if_fail (LRG_IS_LIGHTMAP3D (self));

	g_ptr_array_set_size (self->pages, 0);
	g_hash_table_remove_all (self->entries);
	g_hash_table_remove_all (self->settings);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_PAGES]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_ENTRIES]);
	emit_changed (self);
}

/* ==========================================================================
 * Pages
 * ========================================================================== */

guint
lrg_lightmap3d_add_page (LrgLightmap3D *self,
                         LrgHdrImage   *image)
{
	LightmapPage *p;

	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), G_MAXUINT);
	g_return_val_if_fail (LRG_IS_HDR_IMAGE (image), G_MAXUINT);

	if (self->pages->len >= LRG_LIGHTMAP3D_MAX_PAGES)
		return G_MAXUINT;
	p = g_new0 (LightmapPage, 1);
	p->image = g_object_ref (image);
	p->dirty = TRUE;
	g_ptr_array_add (self->pages, p);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_PAGES]);
	emit_changed (self);
	return self->pages->len - 1;
}

gboolean
lrg_lightmap3d_set_page (LrgLightmap3D *self,
                         guint          page,
                         LrgHdrImage   *image)
{
	LightmapPage *p;

	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), FALSE);
	g_return_val_if_fail (LRG_IS_HDR_IMAGE (image), FALSE);

	if (page >= self->pages->len)
		return FALSE;
	p = g_ptr_array_index (self->pages, page);
	g_set_object (&p->image, image);
	p->dirty = TRUE;
	emit_changed (self);
	return TRUE;
}

LrgHdrImage *
lrg_lightmap3d_get_page (LrgLightmap3D *self,
                         guint          page)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), NULL);

	if (page >= self->pages->len)
		return NULL;
	return ((LightmapPage *)g_ptr_array_index (self->pages, page))->image;
}

guint
lrg_lightmap3d_get_n_pages (LrgLightmap3D *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), 0);
	return self->pages->len;
}

void
lrg_lightmap3d_notify_page_changed (LrgLightmap3D *self,
                                    guint          page)
{
	g_return_if_fail (LRG_IS_LIGHTMAP3D (self));

	if (page >= self->pages->len)
		return;
	((LightmapPage *)g_ptr_array_index (self->pages, page))->dirty = TRUE;
	emit_changed (self);
}

/* ==========================================================================
 * Entries
 * ========================================================================== */

void
lrg_lightmap3d_set_entry (LrgLightmap3D *self,
                          const gchar   *guid,
                          guint          page,
                          const gfloat  *scale_offset)
{
	LightmapEntry *e;
	gboolean       added;

	g_return_if_fail (LRG_IS_LIGHTMAP3D (self));
	g_return_if_fail (guid != NULL && *guid != '\0');
	g_return_if_fail (scale_offset != NULL);

	e = g_new (LightmapEntry, 1);
	e->page = page;
	memcpy (e->scale_offset, scale_offset, sizeof e->scale_offset);
	added = g_hash_table_replace (self->entries, g_strdup (guid), e);
	if (added)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_ENTRIES]);
	emit_changed (self);
}

gboolean
lrg_lightmap3d_get_entry (LrgLightmap3D *self,
                          const gchar   *guid,
                          guint         *out_page,
                          gfloat        *out_scale_offset)
{
	LightmapEntry *e;

	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), FALSE);

	if (guid == NULL)
		return FALSE;
	e = g_hash_table_lookup (self->entries, guid);
	if (e == NULL)
		return FALSE;
	if (out_page != NULL)
		*out_page = e->page;
	if (out_scale_offset != NULL)
		memcpy (out_scale_offset, e->scale_offset, sizeof e->scale_offset);
	return TRUE;
}

gboolean
lrg_lightmap3d_remove_entry (LrgLightmap3D *self,
                             const gchar   *guid)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), FALSE);

	if (guid == NULL || !g_hash_table_remove (self->entries, guid))
		return FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_ENTRIES]);
	emit_changed (self);
	return TRUE;
}

guint
lrg_lightmap3d_get_n_entries (LrgLightmap3D *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), 0);
	return g_hash_table_size (self->entries);
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
	return g_strcmp0 (*(const gchar * const *)a, *(const gchar * const *)b);
}

/* the keys of @table as a sorted, owning string array */
static GPtrArray *
sorted_keys (GHashTable *table)
{
	GPtrArray      *out = g_ptr_array_new_with_free_func (g_free);
	GHashTableIter  it;
	gpointer        key;

	g_hash_table_iter_init (&it, table);
	while (g_hash_table_iter_next (&it, &key, NULL))
		g_ptr_array_add (out, g_strdup (key));
	g_ptr_array_sort (out, compare_strings);
	return out;
}

GPtrArray *
lrg_lightmap3d_get_guids (LrgLightmap3D *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), NULL);
	return sorted_keys (self->entries);
}

/* ==========================================================================
 * CPU sampling
 * ========================================================================== */

gboolean
lrg_lightmap3d_sample_page (LrgLightmap3D *self,
                            guint          page,
                            gfloat         u,
                            gfloat         v,
                            gfloat        *out_rgb)
{
	LrgHdrImage *img;
	const gfloat *px;
	gint         w, h, x0, y0, x1, y1, c;
	gfloat       fx, fy, tx, ty;

	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), FALSE);
	g_return_val_if_fail (out_rgb != NULL, FALSE);

	out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
	img = lrg_lightmap3d_get_page (self, page);
	if (img == NULL)
		return FALSE;

	/* GL_LINEAR with CLAMP_TO_EDGE: texel centres at (i + 0.5) / size */
	w = (gint)lrg_hdr_image_get_width (img);
	h = (gint)lrg_hdr_image_get_height (img);
	px = lrg_hdr_image_get_data (img, NULL);
	fx = CLAMP (u, 0.0f, 1.0f) * (gfloat)w - 0.5f;
	fy = CLAMP (v, 0.0f, 1.0f) * (gfloat)h - 0.5f;
	x0 = (gint)floorf (fx);
	y0 = (gint)floorf (fy);
	tx = fx - (gfloat)x0;
	ty = fy - (gfloat)y0;
	x1 = CLAMP (x0 + 1, 0, w - 1);
	y1 = CLAMP (y0 + 1, 0, h - 1);
	x0 = CLAMP (x0, 0, w - 1);
	y0 = CLAMP (y0, 0, h - 1);

	for (c = 0; c < 3; c++)
	{
		gfloat a = px[((gsize)y0 * (gsize)w + (gsize)x0) * 3 + (gsize)c];
		gfloat b = px[((gsize)y0 * (gsize)w + (gsize)x1) * 3 + (gsize)c];
		gfloat d = px[((gsize)y1 * (gsize)w + (gsize)x0) * 3 + (gsize)c];
		gfloat e = px[((gsize)y1 * (gsize)w + (gsize)x1) * 3 + (gsize)c];
		gfloat top = a + (b - a) * tx;
		gfloat bottom = d + (e - d) * tx;

		out_rgb[c] = (top + (bottom - top) * ty) * self->intensity;
	}
	return TRUE;
}

gboolean
lrg_lightmap3d_sample (LrgLightmap3D *self,
                       const gchar   *guid,
                       const gfloat  *uv2,
                       gfloat        *out_rgb)
{
	guint  page;
	gfloat so[4];

	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), FALSE);
	g_return_val_if_fail (uv2 != NULL && out_rgb != NULL, FALSE);

	if (!lrg_lightmap3d_get_entry (self, guid, &page, so))
	{
		out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
		return FALSE;
	}
	return lrg_lightmap3d_sample_page (self, page, uv2[0] * so[0] + so[2], uv2[1] * so[1] + so[3], out_rgb);
}

/* ==========================================================================
 * Settings and intensity
 * ========================================================================== */

void
lrg_lightmap3d_set_setting (LrgLightmap3D *self,
                            const gchar   *key,
                            gdouble        value)
{
	gdouble *v;

	g_return_if_fail (LRG_IS_LIGHTMAP3D (self));
	g_return_if_fail (key != NULL && *key != '\0');

	v = g_new (gdouble, 1);
	*v = value;
	g_hash_table_replace (self->settings, g_strdup (key), v);
}

gboolean
lrg_lightmap3d_get_setting (LrgLightmap3D *self,
                            const gchar   *key,
                            gdouble       *out_value)
{
	gdouble *v;

	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), FALSE);

	if (key == NULL)
		return FALSE;
	v = g_hash_table_lookup (self->settings, key);
	if (v == NULL)
		return FALSE;
	if (out_value != NULL)
		*out_value = *v;
	return TRUE;
}

GPtrArray *
lrg_lightmap3d_get_setting_keys (LrgLightmap3D *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), NULL);
	return sorted_keys (self->settings);
}

void
lrg_lightmap3d_set_intensity (LrgLightmap3D *self,
                              gfloat         intensity)
{
	g_return_if_fail (LRG_IS_LIGHTMAP3D (self));

	intensity = MAX (intensity, 0.0f);
	if (self->intensity == intensity)
		return;
	self->intensity = intensity;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INTENSITY]);
}

gfloat
lrg_lightmap3d_get_intensity (LrgLightmap3D *self)
{
	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), 1.0f);
	return self->intensity;
}

/* ==========================================================================
 * Files
 *
 * version: 1
 * intensity: 1
 * directional: false            # reserved: only the irradiance term exists
 * pages:
 *   - level-0.hdr               # relative to the index
 * entries:
 *   <guid>: { page: 0, scale_offset: [su, sv, ou, ov] }
 * settings: { texels_per_unit: 8, samples: 256, ... }
 * ========================================================================== */

/* the page file name for page @n of index @path (basename only) */
static gchar *
page_file_name (const gchar *path,
                guint        n)
{
	g_autofree gchar *base = g_path_get_basename (path);
	gchar            *dot = strrchr (base, '.');

	if (dot != NULL && dot != base)
		*dot = '\0';
	return g_strdup_printf ("%s-%u.hdr", base, n);
}

gboolean
lrg_lightmap3d_save (LrgLightmap3D  *self,
                     const gchar    *path,
                     GError        **error)
{
	g_autoptr(YamlNode)      root = NULL;
	g_autoptr(YamlNode)      pages = NULL;
	g_autoptr(YamlNode)      entries = NULL;
	g_autoptr(YamlNode)      settings = NULL;
	g_autoptr(YamlDocument)  doc = NULL;
	g_autoptr(YamlGenerator) gen = NULL;
	g_autoptr(GPtrArray)     guids = NULL;
	g_autoptr(GPtrArray)     keys = NULL;
	g_autofree gchar        *dir = NULL;
	g_autofree gchar        *text = NULL;
	YamlMapping             *map;
	guint                    i;

	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	dir = g_path_get_dirname (path);
	root = yaml_node_new_mapping (NULL);
	map = yaml_node_get_mapping (root);
	yaml_mapping_set_int_member (map, "version", LRG_LIGHTMAP3D_FORMAT_VERSION);
	{
		g_autoptr(YamlNode) n = number_node (self->intensity);

		yaml_mapping_set_member (map, "intensity", n);
	}
	yaml_mapping_set_boolean_member (map, "directional", FALSE);

	/* pages: write each image next to the index */
	pages = yaml_node_new_sequence (NULL);
	for (i = 0; i < self->pages->len; i++)
	{
		LightmapPage     *p = g_ptr_array_index (self->pages, i);
		g_autofree gchar *name = page_file_name (path, i);
		g_autofree gchar *full = g_build_filename (dir, name, NULL);
		g_autoptr(GError) local = NULL;

		if (!lrg_hdr_image_save_to_file (p->image, full, &local))
		{
			g_set_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_IO,
			             "cannot write lightmap page '%s': %s", full, local->message);
			return FALSE;
		}
		yaml_sequence_add_string_element (yaml_node_get_sequence (pages), name);
	}
	yaml_mapping_set_member (map, "pages", pages);

	/* entries, sorted by GUID so saves are stable */
	entries = yaml_node_new_mapping (NULL);
	guids = sorted_keys (self->entries);
	for (i = 0; i < guids->len; i++)
	{
		const gchar         *guid = g_ptr_array_index (guids, i);
		LightmapEntry       *e = g_hash_table_lookup (self->entries, guid);
		g_autoptr(YamlNode)  enode = yaml_node_new_mapping (NULL);
		g_autoptr(YamlNode)  so = yaml_node_new_sequence (NULL);
		guint                k;

		yaml_mapping_set_int_member (yaml_node_get_mapping (enode), "page", (gint64)e->page);
		for (k = 0; k < 4; k++)
		{
			g_autoptr(YamlNode) n = number_node (e->scale_offset[k]);

			yaml_sequence_add_element (yaml_node_get_sequence (so), n);
		}
		yaml_mapping_set_member (yaml_node_get_mapping (enode), "scale_offset", so);
		yaml_mapping_set_member (yaml_node_get_mapping (entries), guid, enode);
	}
	yaml_mapping_set_member (map, "entries", entries);

	/* settings */
	settings = yaml_node_new_mapping (NULL);
	keys = sorted_keys (self->settings);
	for (i = 0; i < keys->len; i++)
	{
		const gchar         *key = g_ptr_array_index (keys, i);
		g_autoptr(YamlNode)  n = number_node (*(gdouble *)g_hash_table_lookup (self->settings, key));

		yaml_mapping_set_member (yaml_node_get_mapping (settings), key, n);
	}
	yaml_mapping_set_member (map, "settings", settings);

	doc = yaml_document_new_with_root (root);
	gen = yaml_generator_new ();
	yaml_generator_set_document (gen, doc);
	text = yaml_generator_to_data (gen, NULL, error);
	if (text == NULL)
		return FALSE;
	if (!g_file_set_contents (path, text, -1, NULL))
	{
		g_set_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_IO,
		             "cannot write lightmap index '%s'", path);
		return FALSE;
	}
	g_free (self->path);
	self->path = g_strdup (path);
	return TRUE;
}

/* reads the `entries:` mapping into @self */
static gboolean
read_entries (LrgLightmap3D  *self,
              YamlNode       *node,
              GError        **error)
{
	YamlMapping      *map;
	g_autoptr(GList)  keys = NULL;
	GList            *l;

	if (yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
	{
		g_set_error_literal (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_FORMAT,
		                     "'entries' must be a mapping");
		return FALSE;
	}
	map = yaml_node_get_mapping (node);
	keys = yaml_mapping_get_members (map);
	for (l = keys; l != NULL; l = l->next)
	{
		YamlNode      *enode = yaml_mapping_get_member (map, l->data);
		YamlMapping   *emap;
		YamlNode      *so;
		LightmapEntry *e;
		gdouble        page = 0.0;
		guint          k;

		if (enode == NULL || yaml_node_get_node_type (enode) != YAML_NODE_MAPPING)
			goto bad;
		emap = yaml_node_get_mapping (enode);
		so = yaml_mapping_get_member (emap, "scale_offset");
		if (!parse_number (yaml_mapping_get_member (emap, "page"), &page) || page < 0.0
		    || so == NULL || yaml_node_get_node_type (so) != YAML_NODE_SEQUENCE
		    || yaml_sequence_get_length (yaml_node_get_sequence (so)) != 4)
			goto bad;
		e = g_new (LightmapEntry, 1);
		e->page = (guint)page;
		for (k = 0; k < 4; k++)
		{
			gdouble v;

			if (!parse_number (yaml_sequence_get_element (yaml_node_get_sequence (so), k), &v))
			{
				g_free (e);
				goto bad;
			}
			e->scale_offset[k] = (gfloat)v;
		}
		g_hash_table_replace (self->entries, g_strdup (l->data), e);
		continue;
bad:
		g_set_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_FORMAT,
		             "entry '%s' needs 'page' and a 4-number 'scale_offset'", (const gchar *)l->data);
		return FALSE;
	}
	return TRUE;
}

LrgLightmap3D *
lrg_lightmap3d_new_from_file (const gchar  *path,
                              GError      **error)
{
	g_autoptr(LrgLightmap3D) self = NULL;
	g_autoptr(YamlParser)    parser = NULL;
	g_autoptr(GError)        local = NULL;
	g_autofree gchar        *dir = NULL;
	YamlNode                *root;
	YamlMapping             *map;
	YamlNode                *node;
	gdouble                  num;

	g_return_val_if_fail (path != NULL, NULL);

	parser = yaml_parser_new ();
	if (!yaml_parser_load_from_file (parser, path, &local))
	{
		g_set_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_IO,
		             "cannot read lightmap '%s': %s", path, local->message);
		return NULL;
	}
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_FORMAT,
		             "lightmap '%s' is not a YAML mapping", path);
		return NULL;
	}
	map = yaml_node_get_mapping (root);
	self = lrg_lightmap3d_new ();
	dir = g_path_get_dirname (path);

	/* version gate */
	if (parse_number (yaml_mapping_get_member (map, "version"), &num) && num > LRG_LIGHTMAP3D_FORMAT_VERSION)
	{
		g_set_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_VERSION,
		             "lightmap '%s' is version %g, this build reads %d", path, num,
		             LRG_LIGHTMAP3D_FORMAT_VERSION);
		return NULL;
	}
	if (parse_number (yaml_mapping_get_member (map, "intensity"), &num))
		self->intensity = (gfloat)MAX (num, 0.0);

	/* pages */
	node = yaml_mapping_get_member (map, "pages");
	if (node != NULL && !yaml_node_is_null (node))
	{
		YamlSequence *seq;
		guint         i, n;

		if (yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		{
			g_set_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_FORMAT,
			             "'pages' of '%s' must be a sequence", path);
			return NULL;
		}
		seq = yaml_node_get_sequence (node);
		n = yaml_sequence_get_length (seq);
		for (i = 0; i < n && i < LRG_LIGHTMAP3D_MAX_PAGES; i++)
		{
			const gchar            *name = yaml_sequence_get_string_element (seq, i);
			g_autofree gchar       *full = NULL;
			g_autoptr(LrgHdrImage)  img = NULL;
			LightmapPage           *p;

			if (name == NULL || *name == '\0')
			{
				g_set_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_FORMAT,
				             "page %u of '%s' is not a file name", i, path);
				return NULL;
			}
			full = g_path_is_absolute (name) ? g_strdup (name) : g_build_filename (dir, name, NULL);
			img = lrg_hdr_image_new_from_file (full, &local);
			if (img == NULL)
			{
				g_set_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_IO,
				             "cannot read lightmap page '%s': %s", full, local->message);
				return NULL;
			}
			p = g_new0 (LightmapPage, 1);
			p->image = g_steal_pointer (&img);
			p->dirty = TRUE;
			g_ptr_array_add (self->pages, p);
		}
	}

	/* entries */
	node = yaml_mapping_get_member (map, "entries");
	if (node != NULL && !yaml_node_is_null (node) && !read_entries (self, node, error))
		return NULL;

	/* settings: numbers and booleans; anything else is skipped */
	node = yaml_mapping_get_member (map, "settings");
	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_MAPPING)
	{
		YamlMapping      *smap = yaml_node_get_mapping (node);
		g_autoptr(GList)  keys = yaml_mapping_get_members (smap);
		GList            *l;

		for (l = keys; l != NULL; l = l->next)
			if (parse_number (yaml_mapping_get_member (smap, l->data), &num))
				lrg_lightmap3d_set_setting (self, l->data, num);
	}

	self->path = g_strdup (path);
	return g_steal_pointer (&self);
}

/* ==========================================================================
 * GPU
 * ========================================================================== */

gboolean
lrg_lightmap3d_upload (LrgLightmap3D  *self,
                       GError        **error)
{
	guint i;

	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), FALSE);

	if (!IsWindowReady ())
	{
		g_set_error_literal (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_GPU,
		                     "uploading a lightmap needs a window (GL context)");
		return FALSE;
	}
	for (i = 0; i < self->pages->len; i++)
	{
		LightmapPage *p = g_ptr_array_index (self->pages, i);
		gfloat       *px;

		if (!p->dirty && p->texture != 0)
			continue;
		if (p->texture != 0)
			rlUnloadTexture (p->texture);
		px = lrg_hdr_image_get_data (p->image, NULL);
		p->texture = rlLoadTexture (px, (gint)lrg_hdr_image_get_width (p->image),
		                            (gint)lrg_hdr_image_get_height (p->image),
		                            RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32, 1);
		if (p->texture == 0)
		{
			g_set_error (error, LRG_LIGHTMAP3D_ERROR, LRG_LIGHTMAP3D_ERROR_GPU,
			             "uploading lightmap page %u failed", i);
			return FALSE;
		}
		rlTextureParameters (p->texture, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
		rlTextureParameters (p->texture, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
		rlTextureParameters (p->texture, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
		rlTextureParameters (p->texture, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
		p->dirty = FALSE;
	}
	return TRUE;
}

void
lrg_lightmap3d_unload_gpu (LrgLightmap3D *self)
{
	guint i;

	g_return_if_fail (LRG_IS_LIGHTMAP3D (self));

	for (i = 0; i < self->pages->len; i++)
	{
		LightmapPage *p = g_ptr_array_index (self->pages, i);

		if (p->texture != 0 && IsWindowReady ())
			rlUnloadTexture (p->texture);
		p->texture = 0;
		p->dirty = TRUE;
	}
}

guint
lrg_lightmap3d_get_page_texture (LrgLightmap3D *self,
                                 guint          page)
{
	LightmapPage *p;

	g_return_val_if_fail (LRG_IS_LIGHTMAP3D (self), 0);

	if (page >= self->pages->len)
		return 0;
	p = g_ptr_array_index (self->pages, page);
	return p->dirty ? 0 : p->texture;
}

/* ==========================================================================
 * Lit-shader binder (LRG_BAKED_GLSL)
 * ========================================================================== */

/*
 * BakedLocs:
 * The hook's uniform locations in one program and the texture units the
 * last bind gave it (-1: none).
 */
typedef struct
{
	gint mode;
	gint sun;
	gint lightmap;
	gint scale_offset;
	gint lightmap_intensity;
	gint probes;
	gint probe_min;
	gint probe_max;
	gint probe_res;
	gint probe_intensity;
	gint unit_probes;
	gint unit_lightmap;
	gint bound_mode;
} BakedLocs;

static GHashTable *baked_locs = NULL;	/* shader id -> BakedLocs * */

/* the locations of @shader_id, re-read when the program was re-created */
static BakedLocs *
baked_get_locs (guint shader_id)
{
	BakedLocs *l;
	gint       check;

	if (baked_locs == NULL)
		baked_locs = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	l = g_hash_table_lookup (baked_locs, GUINT_TO_POINTER (shader_id));
	check = rlGetLocationUniform (shader_id, "lrgBakedMode");
	if (l != NULL && l->mode == check)
		return l;
	l = g_new0 (BakedLocs, 1);
	l->mode = check;
	l->sun = rlGetLocationUniform (shader_id, "lrgBakedSun");
	l->lightmap = rlGetLocationUniform (shader_id, "lrgBakedLightmap");
	l->scale_offset = rlGetLocationUniform (shader_id, "lrgBakedScaleOffset");
	l->lightmap_intensity = rlGetLocationUniform (shader_id, "lrgBakedLightmapIntensity");
	l->probes = rlGetLocationUniform (shader_id, "lrgBakedProbes");
	l->probe_min = rlGetLocationUniform (shader_id, "lrgBakedProbeMin");
	l->probe_max = rlGetLocationUniform (shader_id, "lrgBakedProbeMax");
	l->probe_res = rlGetLocationUniform (shader_id, "lrgBakedProbeRes");
	l->probe_intensity = rlGetLocationUniform (shader_id, "lrgBakedProbeIntensity");
	l->unit_probes = -1;
	l->unit_lightmap = -1;
	g_hash_table_replace (baked_locs, GUINT_TO_POINTER (shader_id), l);
	return l;
}

static void
baked_set_int (gint loc,
               gint v)
{
	if (loc >= 0)
		rlSetUniform (loc, &v, RL_SHADER_UNIFORM_INT, 1);
}

void
lrg_baked_lighting_bind_private (guint  shader_id,
                                 gint  *texture_unit)
{
	BakedLocs *l;
	gint       unit;

	if (shader_id == 0 || texture_unit == NULL)
		return;
	l = baked_get_locs (shader_id);
	unit = *texture_unit;
	rlEnableShader (shader_id);

	/* the probe sampler3D first: it must never share a unit with a
	 * sampler of another type; then the lightmap sampler2D, which may
	 * fall back to unit 0 (texture0's, also a sampler2D) */
	l->unit_probes = -1;
	l->unit_lightmap = -1;
	if (lrg_light_set_hook_unit_usable (unit))
	{
		l->unit_probes = unit;
		unit++;
	}
	if (l->unit_probes >= 0 && lrg_light_set_hook_unit_usable (unit))
	{
		l->unit_lightmap = unit;
		unit++;
	}
	if (l->unit_probes >= 0)
		baked_set_int (l->probes, l->unit_probes);
	baked_set_int (l->lightmap, l->unit_lightmap >= 0 ? l->unit_lightmap : 0);
	baked_set_int (l->mode, 0);
	l->bound_mode = 0;
	*texture_unit = unit;
}

void
lrg_baked_lighting_set_sun_private (guint    shader_id,
                                    gboolean sun_baked)
{
	BakedLocs *l;

	if (shader_id == 0 || baked_locs == NULL)
		return;
	l = g_hash_table_lookup (baked_locs, GUINT_TO_POINTER (shader_id));
	if (l == NULL || l->sun < 0)
		return;
	rlEnableShader (shader_id);
	baked_set_int (l->sun, sun_baked ? 1 : 0);
}

gint
lrg_baked_lighting_set_draw_private (guint          shader_id,
                                     LrgLightmap3D *lightmap,
                                     guint          page,
                                     const gfloat  *scale_offset,
                                     LrgProbeGrid  *grid,
                                     const gdouble *origin)
{
	BakedLocs *l;
	gint       mode = 0;
	guint      tex = 0;

	if (shader_id == 0 || baked_locs == NULL)
		return 0;
	l = g_hash_table_lookup (baked_locs, GUINT_TO_POINTER (shader_id));
	if (l == NULL || l->mode < 0 || l->unit_probes < 0)
		return 0;

	/* the source: a lightmap page that uploads, else a grid that does */
	if (lightmap != NULL && scale_offset != NULL && l->unit_lightmap >= 0
	    && page < lrg_lightmap3d_get_n_pages (lightmap))
	{
		tex = lrg_lightmap3d_get_page_texture (lightmap, page);
		if (tex == 0 && lrg_lightmap3d_upload (lightmap, NULL))
			tex = lrg_lightmap3d_get_page_texture (lightmap, page);
		if (tex != 0)
			mode = 1;
	}
	if (mode == 0 && grid != NULL && lrg_probe_grid_get_n_valid (grid) > 0)
	{
		tex = lrg_probe_grid_get_texture (grid);
		if (tex == 0 && lrg_probe_grid_upload (grid, NULL))
			tex = lrg_probe_grid_get_texture (grid);
		if (tex != 0)
			mode = 2;
	}
	if (mode == 0 && l->bound_mode == 0)
		return 0;

	rlDrawRenderBatchActive ();
	rlEnableShader (shader_id);
	if (mode == 1)
	{
		gfloat k = lrg_lightmap3d_get_intensity (lightmap);

		if (l->scale_offset >= 0)
			rlSetUniform (l->scale_offset, scale_offset, RL_SHADER_UNIFORM_VEC4, 1);
		if (l->lightmap_intensity >= 0)
			rlSetUniform (l->lightmap_intensity, &k, RL_SHADER_UNIFORM_FLOAT, 1);
		rlActiveTextureSlot (l->unit_lightmap);
		rlEnableTexture (tex);
		rlActiveTextureSlot (0);
	}
	else if (mode == 2)
	{
		gfloat mn[3], mx[3], res[3];
		guint  n[3];
		gfloat k = lrg_probe_grid_get_intensity (grid);
		guint  a;

		lrg_probe_grid_get_bounds (grid, mn, mx);
		lrg_probe_grid_get_resolution (grid, &n[0], &n[1], &n[2]);
		for (a = 0; a < 3; a++)
		{
			gfloat o = origin != NULL ? (gfloat)origin[a] : 0.0f;

			mn[a] -= o;
			mx[a] -= o;
			res[a] = (gfloat)n[a];
		}
		if (l->probe_min >= 0)
			rlSetUniform (l->probe_min, mn, RL_SHADER_UNIFORM_VEC3, 1);
		if (l->probe_max >= 0)
			rlSetUniform (l->probe_max, mx, RL_SHADER_UNIFORM_VEC3, 1);
		if (l->probe_res >= 0)
			rlSetUniform (l->probe_res, res, RL_SHADER_UNIFORM_VEC3, 1);
		if (l->probe_intensity >= 0)
			rlSetUniform (l->probe_intensity, &k, RL_SHADER_UNIFORM_FLOAT, 1);
		lrg_baked_bind_texture3d_private (l->unit_probes, tex);
	}
	baked_set_int (l->mode, mode);
	l->bound_mode = mode;
	return mode;
}
