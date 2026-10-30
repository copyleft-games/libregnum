/* lrg-thumbnail-cache.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * On-disk asset thumbnail cache. See the header for the file layout and
 * the threading model. In short:
 *
 *   request()  main thread: hit -> result queue; CPU kind -> worker pool;
 *              GPU kind -> gpu_queue (main thread, renderer callback)
 *   worker     renders + writes the PNG, pushes a Result
 *   process()  main thread: runs a few GPU jobs, drains results, emits
 *              "ready"
 *
 * Workers never touch the cache object: a job carries everything it needs
 * plus a reference to the result queue, and dispose waits for the pool.
 */

#include "lrg-thumbnail-cache.h"
#include "lrg-asset-database.h"
#include "../lrg-enums.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <yaml-glib.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#define MIN_SIZE (16)
#define MAX_SIZE (512)

typedef struct
{
	gchar *path;
	gchar *thumb;   /* nullable: generation failed */
} Result;

typedef struct
{
	gchar       *path;
	gchar       *entry;
	gchar       *dir;
	guint        size;
	GAsyncQueue *results;   /* ref */
} Job;

struct _LrgThumbnailCache
{
	GObject parent_instance;

	gchar                  *dir;
	guint                   size;
	GThreadPool            *pool;
	GAsyncQueue            *results;    /* Result* */
	GHashTable             *pending;    /* path -> TRUE (main thread) */
	GQueue                  gpu_queue;  /* gchar* paths */
	LrgThumbnailRenderFunc  render;
	gpointer                render_data;
	GDestroyNotify          render_destroy;
};

enum
{
	SIGNAL_READY,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (LrgThumbnailCache, lrg_thumbnail_cache, G_TYPE_OBJECT)

static void
result_free (gpointer data)
{
	Result *r = data;

	g_free (r->path);
	g_free (r->thumb);
	g_free (r);
}

static void
push_result (GAsyncQueue *queue,
             const gchar *path,
             const gchar *thumb)
{
	Result *r = g_new0 (Result, 1);

	r->path = g_strdup (path);
	r->thumb = g_strdup (thumb);
	g_async_queue_push (queue, r);
}

static void
job_free (gpointer data)
{
	Job *j = data;

	g_free (j->path);
	g_free (j->entry);
	g_free (j->dir);
	g_async_queue_unref (j->results);
	g_free (j);
}

/* ==========================================================================
 * Keys and files
 * ========================================================================== */

static gchar *
path_hash (const gchar *path)
{
	return g_compute_checksum_for_string (G_CHECKSUM_SHA1, path, -1);
}

gchar *
lrg_thumbnail_cache_make_key (const gchar *path,
                              gint64       mtime_usec,
                              goffset      size)
{
	g_autofree gchar *hash = NULL;

	g_return_val_if_fail (path != NULL, NULL);

	hash = path_hash (path);
	return g_strdup_printf ("%s-%" G_GINT64_FORMAT "-%" G_GOFFSET_FORMAT ".png", hash, mtime_usec, size);
}

/* Absolute, canonical form so "a/../b.png" and "b.png" share an entry. */
static gchar *
canonical (const gchar *path)
{
	return g_canonicalize_filename (path, NULL);
}

static gboolean
stat_file (const gchar *path,
           gint64      *mtime_usec,
           goffset     *size)
{
	g_autoptr(GFile)     file = g_file_new_for_path (path);
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(GDateTime) mtime = NULL;

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_TIME_MODIFIED "," G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC ","
	                          G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
	                          G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (info == NULL || g_file_info_get_file_type (info) != G_FILE_TYPE_REGULAR)
		return FALSE;
	mtime = g_file_info_get_modification_date_time (info);
	*mtime_usec = (mtime != NULL) ? g_date_time_to_unix_usec (mtime) : 0;
	*size = g_file_info_get_size (info);
	return TRUE;
}

/* Removes every entry of @abs_path in @dir except @keep (a basename). */
static guint
sweep (const gchar *dir,
       const gchar *abs_path,
       const gchar *keep)
{
	g_autofree gchar *hash = path_hash (abs_path);
	g_autofree gchar *prefix = g_strconcat (hash, "-", NULL);
	g_autoptr(GDir)   d = g_dir_open (dir, 0, NULL);
	const gchar      *name;
	guint             removed = 0;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *full = NULL;

		if (!g_str_has_prefix (name, prefix) || g_strcmp0 (name, keep) == 0)
			continue;
		full = g_build_filename (dir, name, NULL);
		if (g_unlink (full) == 0)
			removed++;
	}
	return removed;
}

/* Fits @image into size x size (aspect kept; tiny pixel art is enlarged
 * with nearest-neighbour so it stays crisp). */
static GrlImage *
fit_image (GrlImage *image,
           guint     size)
{
	gint   w = grl_image_get_width (image);
	gint   h = grl_image_get_height (image);
	gfloat scale;

	if (w <= 0 || h <= 0)
		return NULL;
	scale = (gfloat) size / (gfloat) MAX (w, h);
	if (scale >= 2.0f)
	{
		gint k = (gint) floorf (scale);

		return grl_image_scaled_nearest (image, w * k, h * k);
	}
	if (scale >= 1.0f)
		return grl_image_copy (image);
	return grl_image_resized (image, MAX (1, (gint) roundf (w * scale)), MAX (1, (gint) roundf (h * scale)));
}

/* Writes @image to @dir/@entry via a temporary file + rename, then sweeps
 * older versions of @abs_path. */
static gchar *
store_to (const gchar  *dir,
          const gchar  *abs_path,
          const gchar  *entry,
          GrlImage     *image,
          guint         size,
          GError      **error)
{
	g_autoptr(GrlImage) fitted = NULL;
	g_autofree gchar   *final = NULL;
	g_autofree gchar   *tmp = NULL;

	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
		             "Cannot create thumbnail cache %s: %s", dir, g_strerror (errno));
		return NULL;
	}
	fitted = fit_image (image, size);
	if (fitted == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Empty thumbnail image");
		return NULL;
	}

	final = g_build_filename (dir, entry, NULL);
	tmp = g_strdup_printf ("%s.%p.tmp.png", final, (gpointer) g_thread_self ());
	if (!grl_image_export (fitted, tmp))
	{
		g_unlink (tmp);
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot write thumbnail %s", tmp);
		return NULL;
	}
	if (g_rename (tmp, final) != 0)
	{
		g_unlink (tmp);
		g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno),
		             "Cannot install thumbnail %s: %s", final, g_strerror (errno));
		return NULL;
	}
	sweep (dir, abs_path, entry);
	return g_steal_pointer (&final);
}

/* ==========================================================================
 * CPU rendering
 * ========================================================================== */

typedef struct
{
	gfloat x;
	gfloat y;
	gfloat r;      /* half extent in world units */
	gint   kind;   /* LrgNodeVisualKind */
} SchemPoint;

static gdouble
seq_double (YamlSequence *seq,
            guint         i,
            gdouble       def)
{
	YamlNode *n = (seq != NULL && i < yaml_sequence_get_length (seq)) ? yaml_sequence_get_element (seq, i) : NULL;

	if (n == NULL || yaml_node_get_node_type (n) != YAML_NODE_SCALAR)
		return def;
	return g_ascii_strtod (yaml_node_get_scalar (n), NULL);
}

static YamlSequence *
member_seq (YamlMapping *map,
            const gchar *key)
{
	YamlNode *n = yaml_mapping_get_member (map, key);

	return (n != NULL && yaml_node_get_node_type (n) == YAML_NODE_SEQUENCE) ? yaml_node_get_sequence (n) : NULL;
}

/* Walks a "nodes"/"children" sequence collecting projected positions.
 * Rotation is ignored: this is a schematic, not a render. */
static void
collect_points (YamlSequence *nodes,
                gfloat        ox,
                gfloat        oy,
                gfloat        oz,
                gboolean      is_2d,
                GArray       *out)
{
	guint i;

	for (i = 0; nodes != NULL && i < yaml_sequence_get_length (nodes); i++)
	{
		YamlNode     *n = yaml_sequence_get_element (nodes, i);
		YamlMapping  *map;
		YamlSequence *loc, *scl;
		YamlNode     *vis;
		SchemPoint    p;
		gfloat        x, y, z, sx, sy, sz;

		if (n == NULL || yaml_node_get_node_type (n) != YAML_NODE_MAPPING)
			continue;
		map = yaml_node_get_mapping (n);
		loc = member_seq (map, "location");
		scl = member_seq (map, "scale");
		x = ox + (gfloat) seq_double (loc, 0, 0.0);
		y = oy + (gfloat) seq_double (loc, 1, 0.0);
		z = oz + (gfloat) seq_double (loc, 2, 0.0);
		sx = (gfloat) fabs (seq_double (scl, 0, 1.0));
		sy = (gfloat) fabs (seq_double (scl, 1, 1.0));
		sz = (gfloat) fabs (seq_double (scl, 2, 1.0));

		p.x = x;
		p.y = is_2d ? -y : z;   /* image rows grow downwards */
		p.r = 0.5f * MAX (sx, is_2d ? sy : sz);
		p.kind = LRG_NODE_VISUAL_NONE;
		vis = yaml_mapping_get_member (map, "visual");
		if (vis != NULL && yaml_node_get_node_type (vis) == YAML_NODE_MAPPING)
		{
			const gchar          *kind = yaml_mapping_get_string_member (yaml_node_get_mapping (vis), "kind");
			g_autoptr(GEnumClass) klass = g_type_class_ref (LRG_TYPE_NODE_VISUAL_KIND);
			GEnumValue           *ev = (kind != NULL) ? g_enum_get_value_by_nick (klass, kind) : NULL;

			if (ev != NULL)
				p.kind = ev->value;
		}
		g_array_append_val (out, p);
		collect_points (member_seq (map, "children"), x, y, z, is_2d, out);
	}
}

static GrlColor
kind_color (gint kind)
{
	GrlColor c = { 166, 173, 200, 255 };

	switch (kind)
	{
	case LRG_NODE_VISUAL_PRIMITIVE:       c.r = 137; c.g = 180; c.b = 250; break;
	case LRG_NODE_VISUAL_MESH_ASSET:      c.r = 116; c.g = 199; c.b = 236; break;
	case LRG_NODE_VISUAL_SPRITE:          c.r = 245; c.g = 194; c.b = 231; break;
	case LRG_NODE_VISUAL_TILEMAP:         c.r = 120; c.g = 200; c.b = 140; break;
	case LRG_NODE_VISUAL_LIGHT:           c.r = 249; c.g = 226; c.b = 175; break;
	case LRG_NODE_VISUAL_CAMERA:          c.r = 180; c.g = 190; c.b = 254; break;
	case LRG_NODE_VISUAL_AUDIO_EMITTER:   c.r = 166; c.g = 227; c.b = 161; break;
	case LRG_NODE_VISUAL_PREFAB_INSTANCE: c.r = 137; c.g = 220; c.b = 235; break;
	case LRG_NODE_VISUAL_CAD_PART:        c.r = 250; c.g = 179; c.b = 135; break;
	default:                              break;
	}
	return c;
}

static GrlImage *
render_schematic (const gchar  *path,
                  guint         size,
                  GError      **error)
{
	g_autoptr(YamlParser) parser = yaml_parser_new ();
	g_autoptr(GArray)     points = g_array_new (FALSE, FALSE, sizeof (SchemPoint));
	YamlNode             *root;
	YamlMapping          *root_map;
	const gchar          *view;
	gboolean              is_2d;
	GrlColor              bg = { 30, 30, 46, 255 };
	GrlColor              grid = { 49, 50, 68, 255 };
	GrlImage             *img;
	gfloat                minx = G_MAXFLOAT, miny = G_MAXFLOAT, maxx = -G_MAXFLOAT, maxy = -G_MAXFLOAT;
	gfloat                scale, span;
	guint                 i;
	gint                  g;

	if (!yaml_parser_load_from_file (parser, path, error))
		return NULL;
	root = yaml_parser_get_root (parser);
	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s is not a level document", path);
		return NULL;
	}
	root_map = yaml_node_get_mapping (root);
	view = yaml_mapping_get_string_member (root_map, "default_view");
	is_2d = (view != NULL && g_ascii_strcasecmp (view, "2d") == 0);
	collect_points (member_seq (root_map, "nodes"), 0.0f, 0.0f, 0.0f, is_2d, points);

	img = grl_image_new_color ((gint) size, (gint) size, &bg);
	for (g = 1; g < 4; g++)
	{
		gint at = (gint) (size * g / 4);

		grl_image_draw_line (img, at, 0, at, (gint) size - 1, &grid);
		grl_image_draw_line (img, 0, at, (gint) size - 1, at, &grid);
	}
	if (points->len == 0)
		return img;

	/* Fit the node extents into the image with a margin. */
	for (i = 0; i < points->len; i++)
	{
		SchemPoint *p = &g_array_index (points, SchemPoint, i);

		minx = MIN (minx, p->x - p->r);
		maxx = MAX (maxx, p->x + p->r);
		miny = MIN (miny, p->y - p->r);
		maxy = MAX (maxy, p->y + p->r);
	}
	span = MAX (MAX (maxx - minx, maxy - miny), 1.0f);
	scale = (size * 0.84f) / span;

	for (i = 0; i < points->len; i++)
	{
		SchemPoint  *p = &g_array_index (points, SchemPoint, i);
		GrlColor     c = kind_color (p->kind);
		gfloat       cx = size * 0.5f + (p->x - (minx + maxx) * 0.5f) * scale;
		gfloat       cy = size * 0.5f + (p->y - (miny + maxy) * 0.5f) * scale;
		gfloat       half = MAX (1.5f, p->r * scale);
		GrlRectangle rect;

		rect.x = cx - half;
		rect.y = cy - half;
		rect.width = half * 2.0f;
		rect.height = half * 2.0f;
		if (p->kind == LRG_NODE_VISUAL_NONE || p->kind == LRG_NODE_VISUAL_LIGHT ||
		    p->kind == LRG_NODE_VISUAL_CAMERA || p->kind == LRG_NODE_VISUAL_AUDIO_EMITTER)
			grl_image_draw_circle (img, (gint) cx, (gint) cy, 2, &c);
		else
			grl_image_draw_rectangle_lines (img, &rect, 1, &c);
	}
	return img;
}

GrlImage *
lrg_thumbnail_cache_render_cpu (const gchar  *path,
                                guint         size,
                                GError      **error)
{
	LrgAssetType type;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	size = CLAMP (size, MIN_SIZE, MAX_SIZE);
	type = lrg_asset_database_classify (path);
	if (type == LRG_ASSET_TYPE_TEXTURE)
	{
		g_autoptr(GrlImage) image = NULL;

		if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No such file %s", path);
			return NULL;
		}
		image = grl_image_new_from_file (path);
		if (image == NULL || !grl_image_is_valid (image))
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Cannot decode image %s", path);
			return NULL;
		}
		return g_steal_pointer (&image);
	}
	if (type == LRG_ASSET_TYPE_LEVEL || type == LRG_ASSET_TYPE_PREFAB)
		return render_schematic (path, size, error);

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "No CPU thumbnail for %s", path);
	return NULL;
}

/* ==========================================================================
 * Worker
 * ========================================================================== */

static void
worker_run (gpointer data,
            gpointer user_data)
{
	Job                *job = data;
	g_autoptr(GError)   error = NULL;
	g_autoptr(GrlImage) image = NULL;
	g_autofree gchar   *thumb = NULL;

	if (g_file_test (job->entry, G_FILE_TEST_EXISTS))
	{
		push_result (job->results, job->path, job->entry);
		job_free (job);
		return;
	}
	image = lrg_thumbnail_cache_render_cpu (job->path, job->size, &error);
	if (image != NULL)
		thumb = store_to (job->dir, job->path, strrchr (job->entry, G_DIR_SEPARATOR) + 1, image, job->size,
		                  &error);
	if (thumb == NULL && error != NULL)
		g_debug ("thumbnail for %s failed: %s", job->path, error->message);
	push_result (job->results, job->path, thumb);
	job_free (job);
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_thumbnail_cache_dispose (GObject *object)
{
	LrgThumbnailCache *self = LRG_THUMBNAIL_CACHE (object);

	/* Drop queued jobs, wait for the running one. */
	if (self->pool != NULL)
	{
		g_thread_pool_free (self->pool, TRUE, TRUE);
		self->pool = NULL;
	}
	if (self->render_destroy != NULL && self->render_data != NULL)
		self->render_destroy (self->render_data);
	self->render = NULL;
	self->render_data = NULL;
	self->render_destroy = NULL;

	G_OBJECT_CLASS (lrg_thumbnail_cache_parent_class)->dispose (object);
}

static void
lrg_thumbnail_cache_finalize (GObject *object)
{
	LrgThumbnailCache *self = LRG_THUMBNAIL_CACHE (object);
	Result            *r;

	while ((r = g_async_queue_try_pop (self->results)) != NULL)
		result_free (r);
	g_async_queue_unref (self->results);
	g_hash_table_unref (self->pending);
	g_queue_clear_full (&self->gpu_queue, g_free);
	g_free (self->dir);

	G_OBJECT_CLASS (lrg_thumbnail_cache_parent_class)->finalize (object);
}

static void
lrg_thumbnail_cache_class_init (LrgThumbnailCacheClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_thumbnail_cache_dispose;
	object_class->finalize = lrg_thumbnail_cache_finalize;

	/**
	 * LrgThumbnailCache::ready:
	 * @self: the cache
	 * @path: the asset path as requested
	 * @thumbnail: (nullable): the thumbnail file, or %NULL if it could not
	 *   be made
	 *
	 * Emitted from lrg_thumbnail_cache_process() when a request finishes.
	 */
	signals[SIGNAL_READY] =
		g_signal_new ("ready", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
}

static void
lrg_thumbnail_cache_init (LrgThumbnailCache *self)
{
	self->size = LRG_THUMBNAIL_DEFAULT_SIZE;
	self->results = g_async_queue_new_full (result_free);
	self->pending = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	g_queue_init (&self->gpu_queue);
}

LrgThumbnailCache *
lrg_thumbnail_cache_new (const gchar *dir)
{
	LrgThumbnailCache *self = g_object_new (LRG_TYPE_THUMBNAIL_CACHE, NULL);

	self->dir = (dir != NULL) ? canonical (dir)
	            : g_build_filename (g_get_user_cache_dir (), "libregnum", "thumbnails", NULL);
	return self;
}

const gchar *
lrg_thumbnail_cache_get_dir (LrgThumbnailCache *self)
{
	g_return_val_if_fail (LRG_IS_THUMBNAIL_CACHE (self), NULL);
	return self->dir;
}

guint
lrg_thumbnail_cache_get_size (LrgThumbnailCache *self)
{
	g_return_val_if_fail (LRG_IS_THUMBNAIL_CACHE (self), 0);
	return self->size;
}

void
lrg_thumbnail_cache_set_size (LrgThumbnailCache *self,
                              guint              size)
{
	g_return_if_fail (LRG_IS_THUMBNAIL_CACHE (self));
	self->size = CLAMP (size, MIN_SIZE, MAX_SIZE);
}

gchar *
lrg_thumbnail_cache_get_entry_path (LrgThumbnailCache *self,
                                    const gchar       *path)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *key = NULL;
	gint64            mtime;
	goffset           size;

	g_return_val_if_fail (LRG_IS_THUMBNAIL_CACHE (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	abs = canonical (path);
	if (!stat_file (abs, &mtime, &size))
		return NULL;
	key = lrg_thumbnail_cache_make_key (abs, mtime, size);
	return g_build_filename (self->dir, key, NULL);
}

gchar *
lrg_thumbnail_cache_lookup (LrgThumbnailCache *self,
                            const gchar       *path)
{
	g_autofree gchar *entry = lrg_thumbnail_cache_get_entry_path (self, path);

	if (entry == NULL || !g_file_test (entry, G_FILE_TEST_IS_REGULAR))
		return NULL;
	return g_steal_pointer (&entry);
}

static gboolean
needs_gpu (const gchar *path)
{
	return lrg_asset_database_classify (path) == LRG_ASSET_TYPE_MODEL;
}

static gboolean
cpu_kind (const gchar *path)
{
	LrgAssetType t = lrg_asset_database_classify (path);

	return t == LRG_ASSET_TYPE_TEXTURE || t == LRG_ASSET_TYPE_LEVEL || t == LRG_ASSET_TYPE_PREFAB;
}

gboolean
lrg_thumbnail_cache_is_supported (LrgThumbnailCache *self,
                                  const gchar       *path)
{
	g_return_val_if_fail (LRG_IS_THUMBNAIL_CACHE (self), FALSE);

	if (path == NULL)
		return FALSE;
	return cpu_kind (path) || (needs_gpu (path) && self->render != NULL);
}

gchar *
lrg_thumbnail_cache_store (LrgThumbnailCache  *self,
                           const gchar        *path,
                           GrlImage           *image,
                           GError            **error)
{
	g_autofree gchar *abs = NULL;
	g_autofree gchar *key = NULL;
	gint64            mtime;
	goffset           size;

	g_return_val_if_fail (LRG_IS_THUMBNAIL_CACHE (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (GRL_IS_IMAGE (image), NULL);

	abs = canonical (path);
	if (!stat_file (abs, &mtime, &size))
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Cannot stat %s", abs);
		return NULL;
	}
	key = lrg_thumbnail_cache_make_key (abs, mtime, size);
	return store_to (self->dir, abs, key, image, self->size, error);
}

gchar *
lrg_thumbnail_cache_generate (LrgThumbnailCache  *self,
                              const gchar        *path,
                              GError            **error)
{
	g_autoptr(GrlImage) image = NULL;
	gchar              *hit;

	g_return_val_if_fail (LRG_IS_THUMBNAIL_CACHE (self), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	if ((hit = lrg_thumbnail_cache_lookup (self, path)) != NULL)
		return hit;

	if (needs_gpu (path))
	{
		g_autofree gchar *abs = canonical (path);

		if (self->render == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
			             "Model thumbnails need a renderer (a window)");
			return NULL;
		}
		image = self->render (abs, self->size, self->render_data);
		if (image == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot render %s", abs);
			return NULL;
		}
	}
	else
	{
		g_autofree gchar *abs = canonical (path);

		image = lrg_thumbnail_cache_render_cpu (abs, self->size, error);
		if (image == NULL)
			return NULL;
	}
	return lrg_thumbnail_cache_store (self, path, image, error);
}

guint
lrg_thumbnail_cache_invalidate (LrgThumbnailCache *self,
                                const gchar       *path)
{
	g_autofree gchar *abs = NULL;

	g_return_val_if_fail (LRG_IS_THUMBNAIL_CACHE (self), 0);
	g_return_val_if_fail (path != NULL, 0);

	abs = canonical (path);
	return sweep (self->dir, abs, NULL);
}

void
lrg_thumbnail_cache_set_renderer (LrgThumbnailCache      *self,
                                  LrgThumbnailRenderFunc  func,
                                  gpointer                user_data,
                                  GDestroyNotify          destroy)
{
	g_return_if_fail (LRG_IS_THUMBNAIL_CACHE (self));

	if (self->render_destroy != NULL && self->render_data != NULL)
		self->render_destroy (self->render_data);
	self->render = func;
	self->render_data = user_data;
	self->render_destroy = destroy;
}

gboolean
lrg_thumbnail_cache_request (LrgThumbnailCache *self,
                             const gchar       *path)
{
	g_autofree gchar *entry = NULL;
	Job              *job;

	g_return_val_if_fail (LRG_IS_THUMBNAIL_CACHE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	if (!lrg_thumbnail_cache_is_supported (self, path))
		return FALSE;
	if (g_hash_table_contains (self->pending, path))
		return TRUE;
	g_hash_table_add (self->pending, g_strdup (path));

	/* Missing file, or a hit: answer on the next process() call. */
	entry = lrg_thumbnail_cache_get_entry_path (self, path);
	if (entry == NULL || g_file_test (entry, G_FILE_TEST_IS_REGULAR))
	{
		push_result (self->results, path, entry);
		return TRUE;
	}

	if (needs_gpu (path))
	{
		g_queue_push_tail (&self->gpu_queue, g_strdup (path));
		return TRUE;
	}

	if (self->pool == NULL)
		self->pool = g_thread_pool_new_full (worker_run, NULL, job_free, 1, FALSE, NULL);
	job = g_new0 (Job, 1);
	job->path = canonical (path);
	job->entry = g_steal_pointer (&entry);
	job->dir = g_strdup (self->dir);
	job->size = self->size;
	job->results = g_async_queue_ref (self->results);
	if (self->pool == NULL || !g_thread_pool_push (self->pool, job, NULL))
	{
		/* No threads available: do it inline rather than never. */
		worker_run (job, NULL);
	}
	return TRUE;
}

guint
lrg_thumbnail_cache_process (LrgThumbnailCache *self,
                             guint              max_gpu_jobs)
{
	Result *r;
	guint   emitted = 0;
	guint   ran = 0;

	g_return_val_if_fail (LRG_IS_THUMBNAIL_CACHE (self), 0);

	/* GPU renders, bounded so a folder of models never stalls a frame. */
	while (ran < max_gpu_jobs && !g_queue_is_empty (&self->gpu_queue))
	{
		g_autofree gchar *path = g_queue_pop_head (&self->gpu_queue);
		g_autoptr(GError) error = NULL;
		g_autofree gchar *thumb = lrg_thumbnail_cache_generate (self, path, &error);

		if (thumb == NULL && error != NULL)
			g_debug ("thumbnail for %s failed: %s", path, error->message);
		push_result (self->results, path, thumb);
		ran++;
	}

	while ((r = g_async_queue_try_pop (self->results)) != NULL)
	{
		/* Results carry the canonical path for worker jobs; report under
		 * the requested spelling when it differs. */
		g_autofree gchar *requested = NULL;
		GHashTableIter    iter;
		gpointer          key;

		if (!g_hash_table_remove (self->pending, r->path))
		{
			g_hash_table_iter_init (&iter, self->pending);
			while (g_hash_table_iter_next (&iter, &key, NULL))
			{
				g_autofree gchar *abs = canonical (key);

				if (g_strcmp0 (abs, r->path) == 0)
				{
					requested = g_strdup (key);
					g_hash_table_iter_remove (&iter);
					break;
				}
			}
		}
		g_signal_emit (self, signals[SIGNAL_READY], 0, requested != NULL ? requested : r->path, r->thumb);
		result_free (r);
		emitted++;
	}
	return emitted;
}

guint
lrg_thumbnail_cache_get_n_pending (LrgThumbnailCache *self)
{
	g_return_val_if_fail (LRG_IS_THUMBNAIL_CACHE (self), 0);
	return g_hash_table_size (self->pending);
}
