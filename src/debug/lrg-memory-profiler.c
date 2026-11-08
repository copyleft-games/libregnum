/* lrg-memory-profiler.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Two instance-count paths:
 *
 *   - g_type_get_instance_count(), only if GOBJECT_DEBUG=instance-count
 *     was in the environment before GObject initialized. A library
 *     constructor is too late (libgobject's own constructors often run
 *     first and read the variable once). The editor can set it at the
 *     top of main(); see the launcher patch.
 *   - A process-wide toggle-ref registry. lrg_memory_profiler_watch_type()
 *     wraps that exact type's constructor; lrg_memory_profiler_track()
 *     counts an existing instance. #GInitiallyUnowned is refused so a
 *     floating object is not sunk. Subclasses are not automatic.
 *
 * When instance counts are live, get_type_counts() reports only that
 * source, so a type is never listed twice. Snapshots and the history
 * ring belong to one profiler; the registry does not.
 */

#include "config.h"

#include "lrg-memory-profiler.h"

#include "../graphics/lrg-material.h"
#include <raylib.h>
#include <rlgl.h>
#include <string.h>

/* raylib keeps this private to rmodels.c. Every loaded material has this
 * many map slots. */
#define MEMORY_MATERIAL_MAPS (12)

/* GLib does not publish this typedef. It matches GObjectClass.constructor. */
typedef GObject *(*MemoryConstructFunc) (GType                  type,
                                         guint                  n_construct_properties,
                                         GObjectConstructParam *construct_properties);

typedef struct
{
	GType                 type;
	MemoryConstructFunc  original;
	gint                  count;
	gboolean              wrapped;
	gpointer              klass;
} TypeWatch;

typedef struct
{
	gchar          *id;
	gchar          *label;
	gint64          time_us;
	LrgMemoryTotals totals;
	GPtrArray      *types;
	GPtrArray      *resources;
} Snapshot;

typedef struct
{
	guint64     instances;
	guint64     cpu_bytes;
	guint64     gpu_bytes;
	GHashTable *type_counts;
} MemSample;

struct _LrgMemoryProfiler
{
	GObject          parent_instance;

	LrgAssetManager *assets;
	GPtrArray       *snapshots;
	GArray          *history;
	guint            next_id;
};

G_DEFINE_FINAL_TYPE (LrgMemoryProfiler, lrg_memory_profiler, G_TYPE_OBJECT)

static GMutex   watch_lock;
static GHashTable *watches;
static MemoryConstructFunc base_constructor;
static GPrivate ctor_depth = G_PRIVATE_INIT (NULL);

enum
{
	PROP_0,
	PROP_ASSET_MANAGER,
	N_PROPS
};

static GParamSpec *memory_props[N_PROPS];

static GQuark track_quark (void);
static TypeWatch * watch_lookup (GType type);
static TypeWatch * watch_ensure (GType type);
static void attach_watch (GObject *object, TypeWatch *watch);
static MemoryConstructFunc real_constructor (GType type);
static GObject * watched_constructor (GType type, guint n_params, GObjectConstructParam *params);
static void fill_totals (LrgMemoryTotals *totals, GPtrArray *types, GPtrArray *resources);
static Snapshot * capture_snapshot (LrgMemoryProfiler *self, const gchar *id, const gchar *label);
static Snapshot * find_snapshot (LrgMemoryProfiler *self, const gchar *id);
static void snapshot_free (Snapshot *snapshot);

/* --------------------------------------------------------------------------
 * Boxed values
 * -------------------------------------------------------------------------- */

LrgMemoryTypeCount *
lrg_memory_type_count_copy (const LrgMemoryTypeCount *count)
{
	LrgMemoryTypeCount *copy;

	if (count == NULL)
		return NULL;
	copy = g_new0 (LrgMemoryTypeCount, 1);
	copy->type_name = g_strdup (count->type_name);
	copy->count = count->count;
	copy->instance_size = count->instance_size;
	copy->bytes = count->bytes;
	copy->source = g_strdup (count->source);
	return copy;
}

void
lrg_memory_type_count_free (LrgMemoryTypeCount *count)
{
	if (count == NULL)
		return;
	g_free (count->type_name);
	g_free (count->source);
	g_free (count);
}

G_DEFINE_BOXED_TYPE (LrgMemoryTypeCount, lrg_memory_type_count,
                     lrg_memory_type_count_copy, lrg_memory_type_count_free)

LrgMemoryResource *
lrg_memory_resource_copy (const LrgMemoryResource *resource)
{
	LrgMemoryResource *copy;

	if (resource == NULL)
		return NULL;
	copy = g_new0 (LrgMemoryResource, 1);
	copy->name = g_strdup (resource->name);
	copy->kind = g_strdup (resource->kind);
	copy->refs = resource->refs;
	copy->cpu_bytes = resource->cpu_bytes;
	copy->gpu_bytes = resource->gpu_bytes;
	copy->details = g_strdup (resource->details);
	return copy;
}

void
lrg_memory_resource_free (LrgMemoryResource *resource)
{
	if (resource == NULL)
		return;
	g_free (resource->name);
	g_free (resource->kind);
	g_free (resource->details);
	g_free (resource);
}

G_DEFINE_BOXED_TYPE (LrgMemoryResource, lrg_memory_resource,
                     lrg_memory_resource_copy, lrg_memory_resource_free)

LrgMemorySnapshotInfo *
lrg_memory_snapshot_info_copy (const LrgMemorySnapshotInfo *info)
{
	LrgMemorySnapshotInfo *copy;

	if (info == NULL)
		return NULL;
	copy = g_new0 (LrgMemorySnapshotInfo, 1);
	copy->id = g_strdup (info->id);
	copy->label = g_strdup (info->label);
	copy->time_us = info->time_us;
	copy->totals = info->totals;
	return copy;
}

void
lrg_memory_snapshot_info_free (LrgMemorySnapshotInfo *info)
{
	if (info == NULL)
		return;
	g_free (info->id);
	g_free (info->label);
	g_free (info);
}

G_DEFINE_BOXED_TYPE (LrgMemorySnapshotInfo, lrg_memory_snapshot_info,
                     lrg_memory_snapshot_info_copy, lrg_memory_snapshot_info_free)

LrgMemoryTypeDelta *
lrg_memory_type_delta_copy (const LrgMemoryTypeDelta *delta)
{
	LrgMemoryTypeDelta *copy;

	if (delta == NULL)
		return NULL;
	copy = g_new0 (LrgMemoryTypeDelta, 1);
	copy->type_name = g_strdup (delta->type_name);
	copy->before = delta->before;
	copy->after = delta->after;
	copy->delta = delta->delta;
	copy->bytes_delta = delta->bytes_delta;
	return copy;
}

void
lrg_memory_type_delta_free (LrgMemoryTypeDelta *delta)
{
	if (delta == NULL)
		return;
	g_free (delta->type_name);
	g_free (delta);
}

G_DEFINE_BOXED_TYPE (LrgMemoryTypeDelta, lrg_memory_type_delta,
                     lrg_memory_type_delta_copy, lrg_memory_type_delta_free)

LrgMemoryResourceDelta *
lrg_memory_resource_delta_copy (const LrgMemoryResourceDelta *delta)
{
	LrgMemoryResourceDelta *copy;

	if (delta == NULL)
		return NULL;
	copy = g_new0 (LrgMemoryResourceDelta, 1);
	copy->name = g_strdup (delta->name);
	copy->kind = g_strdup (delta->kind);
	copy->change = g_strdup (delta->change);
	copy->bytes_delta = delta->bytes_delta;
	return copy;
}

void
lrg_memory_resource_delta_free (LrgMemoryResourceDelta *delta)
{
	if (delta == NULL)
		return;
	g_free (delta->name);
	g_free (delta->kind);
	g_free (delta->change);
	g_free (delta);
}

G_DEFINE_BOXED_TYPE (LrgMemoryResourceDelta, lrg_memory_resource_delta,
                     lrg_memory_resource_delta_copy, lrg_memory_resource_delta_free)

GType
lrg_memory_mesh_attrs_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_MEMORY_MESH_ATTR_POSITIONS, "LRG_MEMORY_MESH_ATTR_POSITIONS", "positions" },
			{ LRG_MEMORY_MESH_ATTR_TEXCOORDS, "LRG_MEMORY_MESH_ATTR_TEXCOORDS", "texcoords" },
			{ LRG_MEMORY_MESH_ATTR_TEXCOORDS2, "LRG_MEMORY_MESH_ATTR_TEXCOORDS2", "texcoords2" },
			{ LRG_MEMORY_MESH_ATTR_NORMALS, "LRG_MEMORY_MESH_ATTR_NORMALS", "normals" },
			{ LRG_MEMORY_MESH_ATTR_TANGENTS, "LRG_MEMORY_MESH_ATTR_TANGENTS", "tangents" },
			{ LRG_MEMORY_MESH_ATTR_COLORS, "LRG_MEMORY_MESH_ATTR_COLORS", "colors" },
			{ LRG_MEMORY_MESH_ATTR_BONES, "LRG_MEMORY_MESH_ATTR_BONES", "bones" },
			{ LRG_MEMORY_MESH_ATTR_ANIM, "LRG_MEMORY_MESH_ATTR_ANIM", "anim" },
			{ 0, NULL, NULL }
		};
		GType type = g_flags_register_static ("LrgMemoryMeshAttrs", values);

		g_once_init_leave (&type_id, type);
	}
	return (GType) type_id;
}

/* --------------------------------------------------------------------------
 * Size estimates
 * -------------------------------------------------------------------------- */

static void
format_layout (gint  format,
               gint *block_w,
               gint *block_h,
               gint *block_bytes,
               gint *bpp)
{
	*block_w = 0;
	*block_h = 0;
	*block_bytes = 0;
	*bpp = 0;

	switch (format)
	{
	case GRL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE:
		*bpp = 1;
		break;
	case GRL_PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R5G6B5:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R5G5B5A1:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R4G4B4A4:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R16:
		*bpp = 2;
		break;
	case GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8:
		*bpp = 3;
		break;
	case GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8:
	case GRL_PIXELFORMAT_UNCOMPRESSED_R32:
		*bpp = 4;
		break;
	case GRL_PIXELFORMAT_UNCOMPRESSED_R16G16B16:
		*bpp = 6;
		break;
	case GRL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:
		*bpp = 8;
		break;
	case GRL_PIXELFORMAT_UNCOMPRESSED_R32G32B32:
		*bpp = 12;
		break;
	case GRL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:
		*bpp = 16;
		break;
	case GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_ETC1_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_ETC2_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_PVRT_RGB:
	case GRL_PIXELFORMAT_COMPRESSED_PVRT_RGBA:
		*block_w = 4;
		*block_h = 4;
		*block_bytes = 8;
		break;
	case GRL_PIXELFORMAT_COMPRESSED_DXT3_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_DXT5_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA:
	case GRL_PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA:
		*block_w = 4;
		*block_h = 4;
		*block_bytes = 16;
		break;
	case GRL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA:
		*block_w = 8;
		*block_h = 8;
		*block_bytes = 16;
		break;
	default:
		/* Unknown positive formats are treated as 32-bit. */
		*bpp = 4;
		break;
	}
}

guint64
lrg_memory_profiler_estimate_texture (gint width,
                                      gint height,
                                      gint mipmaps,
                                      gint format)
{
	gint    block_w;
	gint    block_h;
	gint    block_bytes;
	gint    bpp;
	gint    level;
	guint64 total;

	if (width <= 0 || height <= 0 || mipmaps < 0 || format <= 0)
		return 0;
	if (mipmaps == 0)
		mipmaps = 1;
	if (mipmaps > 16)
		mipmaps = 16;

	format_layout (format, &block_w, &block_h, &block_bytes, &bpp);
	total = 0;
	for (level = 0; level < mipmaps; level++)
	{
		gint w = width >> level;
		gint h = height >> level;

		if (w < 1)
			w = 1;
		if (h < 1)
			h = 1;
		if (block_w > 0)
		{
			gint bx = (w + block_w - 1) / block_w;
			gint by = (h + block_h - 1) / block_h;

			total += (guint64) bx * (guint64) by * (guint64) block_bytes;
		}
		else
			total += (guint64) w * (guint64) h * (guint64) bpp;
	}
	return total;
}

guint64
lrg_memory_profiler_estimate_mesh (gint               vertex_count,
                                   gint               triangle_count,
                                   LrgMemoryMeshAttrs attrs,
                                   gboolean           indexed)
{
	guint64 bytes;

	if (vertex_count < 0 || triangle_count < 0)
		return 0;
	bytes = 0;
	if (attrs & LRG_MEMORY_MESH_ATTR_POSITIONS)
		bytes += (guint64) vertex_count * 12u;
	if (attrs & LRG_MEMORY_MESH_ATTR_TEXCOORDS)
		bytes += (guint64) vertex_count * 8u;
	if (attrs & LRG_MEMORY_MESH_ATTR_TEXCOORDS2)
		bytes += (guint64) vertex_count * 8u;
	if (attrs & LRG_MEMORY_MESH_ATTR_NORMALS)
		bytes += (guint64) vertex_count * 12u;
	if (attrs & LRG_MEMORY_MESH_ATTR_TANGENTS)
		bytes += (guint64) vertex_count * 16u;
	if (attrs & LRG_MEMORY_MESH_ATTR_COLORS)
		bytes += (guint64) vertex_count * 4u;
	if (attrs & LRG_MEMORY_MESH_ATTR_BONES)
		bytes += (guint64) vertex_count * 20u;
	if (attrs & LRG_MEMORY_MESH_ATTR_ANIM)
		bytes += (guint64) vertex_count * 24u;
	if (indexed)
		bytes += (guint64) triangle_count * 3u * 2u;
	return bytes;
}

guint64
lrg_memory_profiler_estimate_pcm (gint64 frames,
                                  gint   bits,
                                  gint   channels)
{
	if (frames < 0 || bits <= 0 || channels <= 0 || (bits % 8) != 0)
		return 0;
	return (guint64) frames * (guint64) channels * (guint64) (bits / 8);
}

/* --------------------------------------------------------------------------
 * Toggle-ref registry (process-wide)
 * -------------------------------------------------------------------------- */

static GQuark
track_quark (void)
{
	static GQuark quark;

	if (G_UNLIKELY (quark == 0))
		quark = g_quark_from_static_string ("lrg-memory-profiler-track");
	return quark;
}

static void
toggle_notify (gpointer  data,
               GObject  *object,
               gboolean  is_last_ref)
{
	TypeWatch *watch = data;

	if (!is_last_ref)
		return;
	/* Steal first so a re-entrant notify cannot decrement twice.
	 * Removing the toggle ref from inside the notify is allowed and
	 * drops the object's last reference. */
	if (g_object_steal_qdata (object, track_quark ()) == NULL)
		return;
	g_atomic_int_add (&watch->count, -1);
	g_object_remove_toggle_ref (object, toggle_notify, watch);
}

static void
attach_watch (GObject   *object,
              TypeWatch *watch)
{
	if (g_object_get_qdata (object, track_quark ()) != NULL)
		return;
	g_object_set_qdata (object, track_quark (), watch);
	g_atomic_int_inc (&watch->count);
	g_object_add_toggle_ref (object, toggle_notify, watch);
}

static TypeWatch *
watch_lookup (GType type)
{
	TypeWatch *watch;

	watch = NULL;
	g_mutex_lock (&watch_lock);
	if (watches != NULL)
		watch = g_hash_table_lookup (watches, GSIZE_TO_POINTER ((gsize) type));
	g_mutex_unlock (&watch_lock);
	return watch;
}

static TypeWatch *
watch_ensure (GType type)
{
	TypeWatch *watch;

	g_mutex_lock (&watch_lock);
	if (watches == NULL)
		watches = g_hash_table_new (g_direct_hash, g_direct_equal);
	watch = g_hash_table_lookup (watches, GSIZE_TO_POINTER ((gsize) type));
	if (watch == NULL)
	{
		watch = g_new0 (TypeWatch, 1);
		watch->type = type;
		g_hash_table_insert (watches, GSIZE_TO_POINTER ((gsize) type), watch);
	}
	g_mutex_unlock (&watch_lock);
	return watch;
}

static void
capture_base (void)
{
	GObjectClass *klass;

	if (base_constructor != NULL)
		return;
	klass = g_type_class_ref (G_TYPE_OBJECT);
	if (klass->constructor != watched_constructor)
		base_constructor = klass->constructor;
}

static MemoryConstructFunc
real_constructor (GType type)
{
	GType t;

	for (t = type; t != 0; t = g_type_parent (t))
	{
		TypeWatch    *watch;
		GObjectClass *klass;

		watch = watch_lookup (t);
		if (watch != NULL && watch->original != NULL && watch->original != watched_constructor)
			return watch->original;
		klass = g_type_class_peek (t);
		if (watch == NULL && klass != NULL && klass->constructor != NULL &&
		    klass->constructor != watched_constructor)
			return klass->constructor;
	}
	return base_constructor;
}

/*
 * Replacing a constructor that chains up to a wrapped parent would call
 * this function again with the same instance type. The outer call uses
 * the type's saved constructor; a re-entry walks from the parent so it
 * reaches the real GObject constructor instead of looping.
 */
static GObject *
watched_constructor (GType                  type,
                     guint                  n_params,
                     GObjectConstructParam *params)
{
	gint                  depth;
	MemoryConstructFunc  chain;
	GObject              *object;
	TypeWatch            *exact;

	depth = GPOINTER_TO_INT (g_private_get (&ctor_depth));
	g_private_set (&ctor_depth, GINT_TO_POINTER (depth + 1));
	if (depth == 0)
		chain = real_constructor (type);
	else
		chain = real_constructor (g_type_parent (type));
	if (chain == NULL || chain == watched_constructor)
		chain = base_constructor;
	object = (chain != NULL) ? chain (type, n_params, params) : NULL;
	if (depth == 0 && object != NULL &&
	    !g_type_is_a (G_OBJECT_TYPE (object), G_TYPE_INITIALLY_UNOWNED))
	{
		exact = watch_lookup (G_OBJECT_TYPE (object));
		if (exact != NULL)
			attach_watch (object, exact);
	}
	g_private_set (&ctor_depth, GINT_TO_POINTER (depth));
	return object;
}

static gboolean
watch_wrap (GType type)
{
	TypeWatch    *watch;
	GObjectClass *klass;

	if (type == 0 || !g_type_is_a (type, G_TYPE_OBJECT))
		return FALSE;
	if (!G_TYPE_IS_INSTANTIATABLE (type))
		return FALSE;
	if (g_type_is_a (type, G_TYPE_INITIALLY_UNOWNED))
		return FALSE;

	capture_base ();
	watch = watch_ensure (type);
	if (watch->wrapped)
		return TRUE;

	klass = g_type_class_ref (type);
	g_mutex_lock (&watch_lock);
	if (!watch->wrapped)
	{
		watch->klass = klass;
		watch->original = klass->constructor;
		klass->constructor = watched_constructor;
		watch->wrapped = TRUE;
		klass = NULL;
	}
	g_mutex_unlock (&watch_lock);
	if (klass != NULL)
		g_type_class_unref (klass);
	return TRUE;
}

/* --------------------------------------------------------------------------
 * Type counts
 * -------------------------------------------------------------------------- */

static gint
cmp_type_name (gconstpointer a,
               gconstpointer b)
{
	const LrgMemoryTypeCount *ta = *(LrgMemoryTypeCount * const *) a;
	const LrgMemoryTypeCount *tb = *(LrgMemoryTypeCount * const *) b;

	return g_strcmp0 (ta->type_name, tb->type_name);
}

static void
add_type_row (GPtrArray   *out,
              GType        type,
              guint64      count,
              const gchar *source)
{
	LrgMemoryTypeCount *row;
	GTypeQuery          query;

	if (count == 0 || type == 0)
		return;
	g_type_query (type, &query);
	row = g_new0 (LrgMemoryTypeCount, 1);
	row->type_name = g_strdup (g_type_name (type));
	row->count = count;
	row->instance_size = query.instance_size;
	row->bytes = count * (guint64) query.instance_size;
	row->source = g_strdup (source);
	g_ptr_array_add (out, row);
}

static void
walk_instance_counts (GType      type,
                      GPtrArray *out)
{
	GType *children;
	guint  n;
	guint  i;
	int    count;

	if (type != G_TYPE_OBJECT && G_TYPE_IS_INSTANTIATABLE (type))
	{
		count = g_type_get_instance_count (type);
		if (count > 0)
			add_type_row (out, type, (guint64) count, LRG_MEMORY_SOURCE_INSTANCE);
	}
	children = g_type_children (type, &n);
	for (i = 0; i < n; i++)
		walk_instance_counts (children[i], out);
	g_free (children);
}

static void
collect_tracked (GPtrArray *out)
{
	GPtrArray *found;
	guint      i;

	found = g_ptr_array_new ();
	g_mutex_lock (&watch_lock);
	if (watches != NULL)
	{
		GHashTableIter iter;
		gpointer       value;

		g_hash_table_iter_init (&iter, watches);
		while (g_hash_table_iter_next (&iter, NULL, &value))
		{
			TypeWatch *watch = value;
			gint       n = g_atomic_int_get (&watch->count);

			if (n > 0)
				g_ptr_array_add (found, watch);
		}
	}
	g_mutex_unlock (&watch_lock);

	for (i = 0; i < found->len; i++)
	{
		TypeWatch *watch = g_ptr_array_index (found, i);
		gint       n = g_atomic_int_get (&watch->count);

		if (n > 0)
			add_type_row (out, watch->type, (guint64) n, LRG_MEMORY_SOURCE_TRACKED);
	}
	g_ptr_array_unref (found);
}

static gpointer
probe_instance_counts (gpointer data)
{
	LrgMemoryProfiler *profiler;
	int                n;

	(void) data;
	/* The temporary instance is alive for the query, so a working
	 * counter reports at least one. new() must not call back here. */
	profiler = lrg_memory_profiler_new ();
	n = g_type_get_instance_count (LRG_TYPE_MEMORY_PROFILER);
	g_object_unref (profiler);
	return GINT_TO_POINTER (n > 0 ? 1 : 0);
}

gboolean
lrg_memory_profiler_instance_counts_available (void)
{
	static GOnce once = G_ONCE_INIT;

	g_once (&once, probe_instance_counts, NULL);
	return GPOINTER_TO_INT (once.retval) != 0;
}

GPtrArray *
lrg_memory_profiler_get_type_counts (LrgMemoryProfiler *self)
{
	GPtrArray *out;

	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), NULL);

	out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_memory_type_count_free);
	if (lrg_memory_profiler_instance_counts_available ())
		walk_instance_counts (G_TYPE_OBJECT, out);
	else
		collect_tracked (out);
	g_ptr_array_sort (out, cmp_type_name);
	return out;
}

/* --------------------------------------------------------------------------
 * Asset residency
 * -------------------------------------------------------------------------- */

static guint
object_refs (GObject *object)
{
	gint n;

	if (object == NULL)
		return 0;
	n = g_atomic_int_get ((gint *) &object->ref_count);
	if (n < 0)
		return 0;
	return (guint) n;
}

static gboolean
texture_is_default (unsigned int id)
{
	if (id == 0)
		return TRUE;
	if (!IsWindowReady ())
		return FALSE;
	return id == rlGetTextureIdDefault ();
}

static void
res_add (GPtrArray   *out,
         const gchar *name,
         const gchar *kind,
         guint        refs,
         guint64      cpu,
         guint64      gpu,
         const gchar *details)
{
	LrgMemoryResource *row;

	row = g_new0 (LrgMemoryResource, 1);
	row->name = g_strdup (name != NULL ? name : "");
	row->kind = g_strdup (kind != NULL ? kind : "other");
	row->refs = refs;
	row->cpu_bytes = cpu;
	row->gpu_bytes = gpu;
	row->details = g_strdup (details != NULL ? details : "");
	g_ptr_array_add (out, row);
}

static void
add_texture (GPtrArray   *out,
             const gchar *name,
             GrlTexture  *texture)
{
	Texture2D       *handle;
	gint             w;
	gint             h;
	gint             mips;
	gint             format;
	guint64          bytes;
	unsigned int     id;
	g_autofree gchar *details = NULL;

	w = grl_texture_get_width (texture);
	h = grl_texture_get_height (texture);
	mips = grl_texture_get_mipmaps (texture);
	format = (gint) grl_texture_get_format (texture);
	bytes = lrg_memory_profiler_estimate_texture (w, h, mips, format);
	handle = grl_texture_get_handle (texture);
	id = (handle != NULL) ? handle->id : 0;
	details = g_strdup_printf ("%dx%d format %d mips %d", w, h, format, mips);
	if (id != 0 && texture_is_default (id))
		res_add (out, name, "texture", object_refs (G_OBJECT (texture)), 0, 0, "default texture");
	else if (id != 0)
		res_add (out, name, "texture", object_refs (G_OBJECT (texture)), 0, bytes, details);
	else
		res_add (out, name, "texture", object_refs (G_OBJECT (texture)), bytes, 0, details);
}

static void
add_font (GPtrArray   *out,
          const gchar *name,
          GrlFont     *font_obj)
{
	Font            *font;
	guint64          cpu;
	guint64          gpu;
	gint             i;
	g_autofree gchar *details = NULL;

	cpu = 0;
	gpu = 0;
	font = grl_font_get_handle (font_obj);
	if (font == NULL)
	{
		res_add (out, name, "font", object_refs (G_OBJECT (font_obj)), 0, 0, "no font handle");
		return;
	}
	if (!texture_is_default (font->texture.id))
		gpu += lrg_memory_profiler_estimate_texture (font->texture.width, font->texture.height,
		                                             font->texture.mipmaps, font->texture.format);
	if (font->glyphs != NULL)
	{
		for (i = 0; i < font->glyphCount; i++)
		{
			Image *image = &font->glyphs[i].image;

			if (image->data == NULL)
				continue;
			cpu += lrg_memory_profiler_estimate_texture (image->width, image->height,
			                                              image->mipmaps, image->format);
		}
	}
	details = g_strdup_printf ("%d glyphs", font->glyphCount);
	res_add (out, name, "font", object_refs (G_OBJECT (font_obj)), cpu, gpu, details);
}

static LrgMemoryMeshAttrs
mesh_attrs (const Mesh *mesh)
{
	LrgMemoryMeshAttrs attrs = LRG_MEMORY_MESH_ATTR_NONE;

	if (mesh->vertices != NULL)
		attrs |= LRG_MEMORY_MESH_ATTR_POSITIONS;
	if (mesh->texcoords != NULL)
		attrs |= LRG_MEMORY_MESH_ATTR_TEXCOORDS;
	if (mesh->texcoords2 != NULL)
		attrs |= LRG_MEMORY_MESH_ATTR_TEXCOORDS2;
	if (mesh->normals != NULL)
		attrs |= LRG_MEMORY_MESH_ATTR_NORMALS;
	if (mesh->tangents != NULL)
		attrs |= LRG_MEMORY_MESH_ATTR_TANGENTS;
	if (mesh->colors != NULL)
		attrs |= LRG_MEMORY_MESH_ATTR_COLORS;
	if (mesh->boneIndices != NULL || mesh->boneWeights != NULL)
		attrs |= LRG_MEMORY_MESH_ATTR_BONES;
	if (mesh->animVertices != NULL || mesh->animNormals != NULL)
		attrs |= LRG_MEMORY_MESH_ATTR_ANIM;
	return attrs;
}

static void
add_model (GPtrArray   *out,
           const gchar *name,
           GrlModel    *model)
{
	Model           *handle;
	GHashTable      *seen;
	guint64          cpu;
	guint64          gpu;
	gint             i;
	gint             map_i;
	g_autofree gchar *details = NULL;

	cpu = 0;
	gpu = 0;
	seen = g_hash_table_new (g_direct_hash, g_direct_equal);
	handle = grl_model_get_handle (model);
	if (handle != NULL && handle->meshes != NULL)
	{
		for (i = 0; i < handle->meshCount; i++)
		{
			const Mesh        *mesh = &handle->meshes[i];
			LrgMemoryMeshAttrs attrs;
			guint64            bytes;
			gint               verts;
			gint               tris;

			verts = mesh->vertexCount > 0 ? mesh->vertexCount : 0;
			tris = mesh->triangleCount > 0 ? mesh->triangleCount : 0;
			attrs = mesh_attrs (mesh);
			bytes = lrg_memory_profiler_estimate_mesh (verts, tris, attrs, mesh->indices != NULL);
			if (attrs != LRG_MEMORY_MESH_ATTR_NONE || mesh->indices != NULL)
				cpu += bytes;
			if (mesh->vaoId != 0)
				gpu += bytes;
		}
	}
	if (handle != NULL && handle->materials != NULL)
	{
		for (i = 0; i < handle->materialCount; i++)
		{
			Material *mat = &handle->materials[i];

			if (mat->maps == NULL)
				continue;
			for (map_i = 0; map_i < MEMORY_MATERIAL_MAPS; map_i++)
			{
				Texture2D    tex = mat->maps[map_i].texture;
				unsigned int id = tex.id;

				if (texture_is_default (id))
					continue;
				if (g_hash_table_contains (seen, GUINT_TO_POINTER (id)))
					continue;
				g_hash_table_add (seen, GUINT_TO_POINTER (id));
				gpu += lrg_memory_profiler_estimate_texture (tex.width, tex.height, tex.mipmaps, tex.format);
			}
		}
	}
	g_hash_table_unref (seen);
	if (handle != NULL)
		details = g_strdup_printf ("%d meshes, %d materials", handle->meshCount, handle->materialCount);
	else
		details = g_strdup ("no model handle");
	res_add (out, name, "model", object_refs (G_OBJECT (model)), cpu, gpu, details);
}

static void
on_cached (const gchar *name,
           const gchar *kind,
           GObject     *asset,
           guint        count,
           gpointer     user_data)
{
	GPtrArray *out = user_data;

	if (g_strcmp0 (kind, "animation") == 0)
	{
		g_autofree gchar *details = g_strdup_printf ("%u clips", count);

		res_add (out, name, "animation", count, 0, 0, details);
		return;
	}
	if (asset == NULL)
		return;
	if (g_strcmp0 (kind, "texture") == 0 && GRL_IS_TEXTURE (asset))
		add_texture (out, name, GRL_TEXTURE (asset));
	else if (g_strcmp0 (kind, "font") == 0 && GRL_IS_FONT (asset))
		add_font (out, name, GRL_FONT (asset));
	else if (g_strcmp0 (kind, "sound") == 0)
		res_add (out, name, "sound", object_refs (asset), 0, 0, "PCM size is private to graylib");
	else if (g_strcmp0 (kind, "music") == 0 && GRL_IS_MUSIC (asset))
	{
		g_autofree gchar *details = NULL;

		details = g_strdup_printf ("streamed, duration %.3f s", grl_music_get_time_length (GRL_MUSIC (asset)));
		res_add (out, name, "music", object_refs (asset), 0, 0, details);
	}
	else if (g_strcmp0 (kind, "model") == 0 && GRL_IS_MODEL (asset))
		add_model (out, name, GRL_MODEL (asset));
	else if (g_strcmp0 (kind, "material") == 0)
	{
		GTypeQuery        query;
		const gchar      *path = "";

		g_type_query (G_OBJECT_TYPE (asset), &query);
		if (LRG_IS_MATERIAL (asset) && lrg_material_get_path (LRG_MATERIAL (asset)) != NULL)
			path = lrg_material_get_path (LRG_MATERIAL (asset));
		res_add (out, name, "material", object_refs (asset), query.instance_size, 0, path);
	}
	else if (g_strcmp0 (kind, "object") == 0)
	{
		GTypeQuery query;

		g_type_query (G_OBJECT_TYPE (asset), &query);
		res_add (out, name, "object", object_refs (asset), query.instance_size, 0, g_type_name (G_OBJECT_TYPE (asset)));
	}
	else
		res_add (out, name, kind, object_refs (asset), 0, 0, "");
}

static gint
cmp_resource (gconstpointer a,
              gconstpointer b)
{
	const LrgMemoryResource *ra = *(LrgMemoryResource * const *) a;
	const LrgMemoryResource *rb = *(LrgMemoryResource * const *) b;
	gint                      c;

	c = g_strcmp0 (ra->kind, rb->kind);
	if (c != 0)
		return c;
	return g_strcmp0 (ra->name, rb->name);
}

GPtrArray *
lrg_memory_profiler_get_resources (LrgMemoryProfiler *self)
{
	GPtrArray *out;

	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), NULL);

	out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_memory_resource_free);
	if (self->assets != NULL)
		lrg_asset_manager_foreach_cached (self->assets, on_cached, out);
	g_ptr_array_sort (out, cmp_resource);
	return out;
}

/* --------------------------------------------------------------------------
 * Totals, snapshots, diff, history
 * -------------------------------------------------------------------------- */

static void
fill_totals (LrgMemoryTotals *totals,
             GPtrArray       *types,
             GPtrArray       *resources)
{
	guint i;

	memset (totals, 0, sizeof *totals);
	totals->types = types->len;
	totals->resources = resources->len;
	for (i = 0; i < types->len; i++)
	{
		LrgMemoryTypeCount *row = g_ptr_array_index (types, i);

		totals->instances += row->count;
		totals->type_bytes += row->bytes;
	}
	for (i = 0; i < resources->len; i++)
	{
		LrgMemoryResource *row = g_ptr_array_index (resources, i);

		totals->cpu_bytes += row->cpu_bytes;
		totals->gpu_bytes += row->gpu_bytes;
	}
}

void
lrg_memory_profiler_get_totals (LrgMemoryProfiler *self,
                                LrgMemoryTotals   *totals)
{
	g_autoptr(GPtrArray) types = NULL;
	g_autoptr(GPtrArray) resources = NULL;

	g_return_if_fail (LRG_IS_MEMORY_PROFILER (self));
	g_return_if_fail (totals != NULL);

	types = lrg_memory_profiler_get_type_counts (self);
	resources = lrg_memory_profiler_get_resources (self);
	fill_totals (totals, types, resources);
}

static void
snapshot_free (Snapshot *snapshot)
{
	if (snapshot == NULL)
		return;
	g_free (snapshot->id);
	g_free (snapshot->label);
	g_clear_pointer (&snapshot->types, g_ptr_array_unref);
	g_clear_pointer (&snapshot->resources, g_ptr_array_unref);
	g_free (snapshot);
}

static Snapshot *
capture_snapshot (LrgMemoryProfiler *self,
                  const gchar       *id,
                  const gchar       *label)
{
	Snapshot *snapshot;

	snapshot = g_new0 (Snapshot, 1);
	snapshot->id = g_strdup (id);
	snapshot->label = g_strdup (label != NULL ? label : "");
	snapshot->time_us = g_get_real_time ();
	snapshot->types = lrg_memory_profiler_get_type_counts (self);
	snapshot->resources = lrg_memory_profiler_get_resources (self);
	fill_totals (&snapshot->totals, snapshot->types, snapshot->resources);
	return snapshot;
}

static Snapshot *
find_snapshot (LrgMemoryProfiler *self,
               const gchar       *id)
{
	guint i;

	if (id == NULL)
		return NULL;
	for (i = 0; i < self->snapshots->len; i++)
	{
		Snapshot *snapshot = g_ptr_array_index (self->snapshots, i);

		if (g_strcmp0 (snapshot->id, id) == 0)
			return snapshot;
	}
	return NULL;
}

gchar *
lrg_memory_profiler_snapshot (LrgMemoryProfiler *self,
                              const gchar       *label)
{
	Snapshot *snapshot;
	gchar    *id;

	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), NULL);

	self->next_id++;
	id = g_strdup_printf ("m%u", self->next_id);
	snapshot = capture_snapshot (self, id, label);
	g_free (id);
	g_ptr_array_add (self->snapshots, snapshot);
	return g_strdup (snapshot->id);
}

GPtrArray *
lrg_memory_profiler_list_snapshots (LrgMemoryProfiler *self)
{
	GPtrArray *out;
	guint      i;

	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), NULL);

	out = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_memory_snapshot_info_free);
	for (i = 0; i < self->snapshots->len; i++)
	{
		Snapshot              *snapshot = g_ptr_array_index (self->snapshots, i);
		LrgMemorySnapshotInfo *info = g_new0 (LrgMemorySnapshotInfo, 1);

		info->id = g_strdup (snapshot->id);
		info->label = g_strdup (snapshot->label);
		info->time_us = snapshot->time_us;
		info->totals = snapshot->totals;
		g_ptr_array_add (out, info);
	}
	return out;
}

guint
lrg_memory_profiler_clear_snapshots (LrgMemoryProfiler *self)
{
	guint n;

	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), 0);

	n = self->snapshots->len;
	g_ptr_array_set_size (self->snapshots, 0);
	return n;
}

static gint
cmp_type_delta (gconstpointer a,
                gconstpointer b)
{
	const LrgMemoryTypeDelta *ta = *(LrgMemoryTypeDelta * const *) a;
	const LrgMemoryTypeDelta *tb = *(LrgMemoryTypeDelta * const *) b;

	return g_strcmp0 (ta->type_name, tb->type_name);
}

static gint
cmp_res_delta (gconstpointer a,
               gconstpointer b)
{
	const LrgMemoryResourceDelta *ta = *(LrgMemoryResourceDelta * const *) a;
	const LrgMemoryResourceDelta *tb = *(LrgMemoryResourceDelta * const *) b;
	gint                           c;

	c = g_strcmp0 (ta->name, tb->name);
	if (c != 0)
		return c;
	return g_strcmp0 (ta->kind, tb->kind);
}

static GHashTable *
type_index (GPtrArray *rows)
{
	GHashTable *map;
	guint       i;

	map = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < rows->len; i++)
	{
		LrgMemoryTypeCount *row = g_ptr_array_index (rows, i);

		g_hash_table_insert (map, row->type_name, row);
	}
	return map;
}

static gchar *
resource_key (const LrgMemoryResource *resource)
{
	return g_strdup_printf ("%s\n%s", resource->kind != NULL ? resource->kind : "",
	                        resource->name != NULL ? resource->name : "");
}

static GHashTable *
resource_index (GPtrArray *rows)
{
	GHashTable *map;
	guint       i;

	map = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (i = 0; i < rows->len; i++)
	{
		LrgMemoryResource *row = g_ptr_array_index (rows, i);

		g_hash_table_insert (map, resource_key (row), row);
	}
	return map;
}

static gint64
bytes_delta (guint64 before,
             guint64 after)
{
	if (after >= before)
		return (gint64) (after - before);
	return -((gint64) (before - after));
}

static void
diff_types (Snapshot  *from,
            Snapshot  *to,
            GPtrArray *out)
{
	g_autoptr(GHashTable) from_map = type_index (from->types);
	g_autoptr(GHashTable) to_map = type_index (to->types);
	g_autoptr(GHashTable) seen = g_hash_table_new (g_str_hash, g_str_equal);
	guint                 i;

	for (i = 0; i < from->types->len + to->types->len; i++)
	{
		GPtrArray          *src = (i < from->types->len) ? from->types : to->types;
		guint               idx = (i < from->types->len) ? i : (i - from->types->len);
		LrgMemoryTypeCount *row = g_ptr_array_index (src, idx);
		LrgMemoryTypeCount *before;
		LrgMemoryTypeCount *after;
		LrgMemoryTypeDelta *delta;
		guint64             bcount;
		guint64             acount;
		guint64             bbytes;
		guint64             abytes;

		if (g_hash_table_contains (seen, row->type_name))
			continue;
		g_hash_table_add (seen, row->type_name);
		before = g_hash_table_lookup (from_map, row->type_name);
		after = g_hash_table_lookup (to_map, row->type_name);
		bcount = (before != NULL) ? before->count : 0;
		acount = (after != NULL) ? after->count : 0;
		bbytes = (before != NULL) ? before->bytes : 0;
		abytes = (after != NULL) ? after->bytes : 0;
		if (bcount == acount && bbytes == abytes)
			continue;
		delta = g_new0 (LrgMemoryTypeDelta, 1);
		delta->type_name = g_strdup (row->type_name);
		delta->before = bcount;
		delta->after = acount;
		delta->delta = bytes_delta (bcount, acount);
		delta->bytes_delta = bytes_delta (bbytes, abytes);
		g_ptr_array_add (out, delta);
	}
	g_ptr_array_sort (out, cmp_type_delta);
}

static void
diff_resources (Snapshot  *from,
                Snapshot  *to,
                GPtrArray *out)
{
	g_autoptr(GHashTable) from_map = resource_index (from->resources);
	g_autoptr(GHashTable) to_map = resource_index (to->resources);
	g_autoptr(GHashTable) seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	guint                 i;

	for (i = 0; i < from->resources->len + to->resources->len; i++)
	{
		GPtrArray              *src = (i < from->resources->len) ? from->resources : to->resources;
		guint                   idx = (i < from->resources->len) ? i : (i - from->resources->len);
		LrgMemoryResource      *row = g_ptr_array_index (src, idx);
		g_autofree gchar       *key = resource_key (row);
		LrgMemoryResource      *before;
		LrgMemoryResource      *after;
		LrgMemoryResourceDelta *delta;
		guint64                 bsum;
		guint64                 asum;
		const gchar            *change;

		if (g_hash_table_contains (seen, key))
			continue;
		g_hash_table_add (seen, g_strdup (key));
		before = g_hash_table_lookup (from_map, key);
		after = g_hash_table_lookup (to_map, key);
		bsum = (before != NULL) ? before->cpu_bytes + before->gpu_bytes : 0;
		asum = (after != NULL) ? after->cpu_bytes + after->gpu_bytes : 0;
		if (before == NULL)
			change = "added";
		else if (after == NULL)
			change = "removed";
		else if (before->cpu_bytes != after->cpu_bytes || before->gpu_bytes != after->gpu_bytes ||
		         before->refs != after->refs)
			change = "changed";
		else
			continue;
		delta = g_new0 (LrgMemoryResourceDelta, 1);
		delta->name = g_strdup (row->name);
		delta->kind = g_strdup (row->kind);
		delta->change = g_strdup (change);
		delta->bytes_delta = bytes_delta (bsum, asum);
		g_ptr_array_add (out, delta);
	}
	g_ptr_array_sort (out, cmp_res_delta);
}

gboolean
lrg_memory_profiler_diff (LrgMemoryProfiler  *self,
                          const gchar        *from_id,
                          const gchar        *to_id,
                          GPtrArray         **type_deltas,
                          GPtrArray         **resource_deltas,
                          GError            **error)
{
	Snapshot  *from;
	Snapshot  *to;
	Snapshot  *live;
	GPtrArray *types;
	GPtrArray *resources;

	if (type_deltas != NULL)
		*type_deltas = NULL;
	if (resource_deltas != NULL)
		*resource_deltas = NULL;
	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), FALSE);

	if (from_id == NULL || from_id[0] == '\0')
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "from is required");
		return FALSE;
	}
	from = find_snapshot (self, from_id);
	if (from == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Unknown snapshot '%s'", from_id);
		return FALSE;
	}
	live = NULL;
	if (to_id == NULL)
	{
		live = capture_snapshot (self, "live", NULL);
		to = live;
	}
	else
	{
		to = find_snapshot (self, to_id);
		if (to == NULL)
		{
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Unknown snapshot '%s'", to_id);
			return FALSE;
		}
	}

	types = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_memory_type_delta_free);
	resources = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_memory_resource_delta_free);
	diff_types (from, to, types);
	diff_resources (from, to, resources);
	snapshot_free (live);
	if (type_deltas != NULL)
		*type_deltas = types;
	else
		g_ptr_array_unref (types);
	if (resource_deltas != NULL)
		*resource_deltas = resources;
	else
		g_ptr_array_unref (resources);
	return TRUE;
}

static void
sample_clear (gpointer data)
{
	MemSample *sample = data;

	g_clear_pointer (&sample->type_counts, g_hash_table_unref);
}

void
lrg_memory_profiler_record_sample (LrgMemoryProfiler *self)
{
	g_autoptr(GPtrArray) types = NULL;
	g_autoptr(GPtrArray) resources = NULL;
	LrgMemoryTotals      totals;
	MemSample            sample;
	guint                i;

	g_return_if_fail (LRG_IS_MEMORY_PROFILER (self));

	types = lrg_memory_profiler_get_type_counts (self);
	resources = lrg_memory_profiler_get_resources (self);
	fill_totals (&totals, types, resources);
	memset (&sample, 0, sizeof sample);
	sample.instances = totals.instances;
	sample.cpu_bytes = totals.cpu_bytes;
	sample.gpu_bytes = totals.gpu_bytes;
	sample.type_counts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	for (i = 0; i < types->len; i++)
	{
		LrgMemoryTypeCount *row = g_ptr_array_index (types, i);
		guint64            *count;

		if (row->count == 0 || row->type_name == NULL)
			continue;
		count = g_new (guint64, 1);
		*count = row->count;
		g_hash_table_insert (sample.type_counts, g_strdup (row->type_name), count);
	}
	g_array_append_val (self->history, sample);
	while (self->history->len > LRG_MEMORY_HISTORY_CAP)
		g_array_remove_index (self->history, 0);
}

void
lrg_memory_profiler_get_history (LrgMemoryProfiler *self,
                                 const gchar       *type_name,
                                 guint              max_samples,
                                 guint64          **instances,
                                 guint64          **cpu_bytes,
                                 guint64          **gpu_bytes,
                                 guint64          **type_counts,
                                 guint             *n_samples)
{
	guint    len;
	guint    start;
	guint    n;
	guint    i;
	guint64 *inst;
	guint64 *cpu;
	guint64 *gpu;
	guint64 *types;

	g_return_if_fail (LRG_IS_MEMORY_PROFILER (self));
	g_return_if_fail (n_samples != NULL);

	len = self->history->len;
	start = 0;
	if (max_samples > 0 && max_samples < len)
		start = len - max_samples;
	n = len - start;
	inst = NULL;
	cpu = NULL;
	gpu = NULL;
	types = NULL;
	if (n > 0 && instances != NULL)
		inst = g_new (guint64, n);
	if (n > 0 && cpu_bytes != NULL)
		cpu = g_new (guint64, n);
	if (n > 0 && gpu_bytes != NULL)
		gpu = g_new (guint64, n);
	if (n > 0 && type_counts != NULL && type_name != NULL)
		types = g_new0 (guint64, n);
	for (i = 0; i < n; i++)
	{
		MemSample *sample = &g_array_index (self->history, MemSample, start + i);

		if (inst != NULL)
			inst[i] = sample->instances;
		if (cpu != NULL)
			cpu[i] = sample->cpu_bytes;
		if (gpu != NULL)
			gpu[i] = sample->gpu_bytes;
		if (types != NULL)
		{
			guint64 *count = g_hash_table_lookup (sample->type_counts, type_name);

			types[i] = (count != NULL) ? *count : 0;
		}
	}
	if (instances != NULL)
		*instances = inst;
	if (cpu_bytes != NULL)
		*cpu_bytes = cpu;
	if (gpu_bytes != NULL)
		*gpu_bytes = gpu;
	if (type_counts != NULL)
		*type_counts = types;
	*n_samples = n;
}

/* --------------------------------------------------------------------------
 * Object
 * -------------------------------------------------------------------------- */

static void
lrg_memory_profiler_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
	LrgMemoryProfiler *self = LRG_MEMORY_PROFILER (object);

	switch (prop_id)
	{
	case PROP_ASSET_MANAGER:
		lrg_memory_profiler_set_asset_manager (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_memory_profiler_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
	LrgMemoryProfiler *self = LRG_MEMORY_PROFILER (object);

	switch (prop_id)
	{
	case PROP_ASSET_MANAGER:
		g_value_set_object (value, self->assets);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_memory_profiler_dispose (GObject *object)
{
	LrgMemoryProfiler *self = LRG_MEMORY_PROFILER (object);

	g_clear_object (&self->assets);
	g_clear_pointer (&self->snapshots, g_ptr_array_unref);
	g_clear_pointer (&self->history, g_array_unref);
	G_OBJECT_CLASS (lrg_memory_profiler_parent_class)->dispose (object);
}

static void
lrg_memory_profiler_class_init (LrgMemoryProfilerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = lrg_memory_profiler_set_property;
	object_class->get_property = lrg_memory_profiler_get_property;
	object_class->dispose = lrg_memory_profiler_dispose;

	memory_props[PROP_ASSET_MANAGER] =
		g_param_spec_object ("asset-manager", NULL, NULL,
		                     LRG_TYPE_ASSET_MANAGER,
		                     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
	g_object_class_install_properties (object_class, N_PROPS, memory_props);
}

static void
lrg_memory_profiler_init (LrgMemoryProfiler *self)
{
	self->snapshots = g_ptr_array_new_with_free_func ((GDestroyNotify) snapshot_free);
	self->history = g_array_new (FALSE, TRUE, sizeof (MemSample));
	g_array_set_clear_func (self->history, sample_clear);
}

static gpointer
create_default (gpointer data)
{
	(void) data;
	return lrg_memory_profiler_new ();
}

LrgMemoryProfiler *
lrg_memory_profiler_new (void)
{
	return g_object_new (LRG_TYPE_MEMORY_PROFILER, NULL);
}

LrgMemoryProfiler *
lrg_memory_profiler_get_default (void)
{
	static GOnce once = G_ONCE_INIT;

	g_once (&once, create_default, NULL);
	return once.retval;
}

gboolean
lrg_memory_profiler_watch_type (LrgMemoryProfiler *self,
                                GType              type)
{
	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), FALSE);
	return watch_wrap (type);
}

gboolean
lrg_memory_profiler_track (LrgMemoryProfiler *self,
                           GObject           *object)
{
	GType      type;
	TypeWatch *watch;

	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), FALSE);
	g_return_val_if_fail (G_IS_OBJECT (object), FALSE);

	type = G_OBJECT_TYPE (object);
	if (g_type_is_a (type, G_TYPE_INITIALLY_UNOWNED))
		return FALSE;
	watch = watch_ensure (type);
	attach_watch (object, watch);
	return TRUE;
}

gint
lrg_memory_profiler_get_tracked_count (LrgMemoryProfiler *self,
                                       GType              type)
{
	TypeWatch *watch;

	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), 0);

	watch = watch_lookup (type);
	if (watch == NULL)
		return 0;
	return g_atomic_int_get (&watch->count);
}

gint
lrg_memory_profiler_get_tracked_instances (LrgMemoryProfiler *self)
{
	gint sum;

	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), 0);

	sum = 0;
	g_mutex_lock (&watch_lock);
	if (watches != NULL)
	{
		GHashTableIter iter;
		gpointer       value;

		g_hash_table_iter_init (&iter, watches);
		while (g_hash_table_iter_next (&iter, NULL, &value))
		{
			TypeWatch *watch = value;

			sum += g_atomic_int_get (&watch->count);
		}
	}
	g_mutex_unlock (&watch_lock);
	return sum;
}

void
lrg_memory_profiler_set_asset_manager (LrgMemoryProfiler *self,
                                       LrgAssetManager   *manager)
{
	g_return_if_fail (LRG_IS_MEMORY_PROFILER (self));
	g_return_if_fail (manager == NULL || LRG_IS_ASSET_MANAGER (manager));

	if (g_set_object (&self->assets, manager))
		g_object_notify_by_pspec (G_OBJECT (self), memory_props[PROP_ASSET_MANAGER]);
}

LrgAssetManager *
lrg_memory_profiler_get_asset_manager (LrgMemoryProfiler *self)
{
	g_return_val_if_fail (LRG_IS_MEMORY_PROFILER (self), NULL);
	return self->assets;
}
