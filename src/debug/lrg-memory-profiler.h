/* lrg-memory-profiler.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMemoryProfiler: GObject instance counts, asset residency and
 * VRAM estimates. Instance counts come from g_type_get_instance_count()
 * when GOBJECT_DEBUG=instance-count was set before GObject initialized,
 * and from a process-wide toggle-ref registry otherwise (and for any
 * type lrg_memory_profiler_watch_type() or lrg_memory_profiler_track()
 * was asked to follow). See docs/modules/debug/memory-profiler.org.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../core/lrg-asset-manager.h"

G_BEGIN_DECLS

/**
 * LrgMemoryMeshAttrs:
 * @LRG_MEMORY_MESH_ATTR_NONE: no vertex attributes
 * @LRG_MEMORY_MESH_ATTR_POSITIONS: 3 floats per vertex
 * @LRG_MEMORY_MESH_ATTR_TEXCOORDS: 2 floats per vertex
 * @LRG_MEMORY_MESH_ATTR_TEXCOORDS2: 2 floats per vertex
 * @LRG_MEMORY_MESH_ATTR_NORMALS: 3 floats per vertex
 * @LRG_MEMORY_MESH_ATTR_TANGENTS: 4 floats per vertex
 * @LRG_MEMORY_MESH_ATTR_COLORS: 4 bytes per vertex
 * @LRG_MEMORY_MESH_ATTR_BONES: 4 bone indices and 4 float weights
 * @LRG_MEMORY_MESH_ATTR_ANIM: animated positions and normals (3 floats each)
 *
 * Vertex attributes lrg_memory_profiler_estimate_mesh() sizes. Combine
 * them with bitwise or.
 *
 * Since: 0.9.0
 */
typedef enum
{
	LRG_MEMORY_MESH_ATTR_NONE       = 0,
	LRG_MEMORY_MESH_ATTR_POSITIONS  = 1 << 0,
	LRG_MEMORY_MESH_ATTR_TEXCOORDS  = 1 << 1,
	LRG_MEMORY_MESH_ATTR_TEXCOORDS2 = 1 << 2,
	LRG_MEMORY_MESH_ATTR_NORMALS    = 1 << 3,
	LRG_MEMORY_MESH_ATTR_TANGENTS   = 1 << 4,
	LRG_MEMORY_MESH_ATTR_COLORS     = 1 << 5,
	LRG_MEMORY_MESH_ATTR_BONES      = 1 << 6,
	LRG_MEMORY_MESH_ATTR_ANIM       = 1 << 7
} LrgMemoryMeshAttrs;

LRG_AVAILABLE_IN_ALL
GType lrg_memory_mesh_attrs_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MEMORY_MESH_ATTRS (lrg_memory_mesh_attrs_get_type ())

/**
 * LRG_MEMORY_SOURCE_INSTANCE:
 *
 * Value of #LrgMemoryTypeCount.source when the count came from
 * g_type_get_instance_count().
 *
 * Since: 0.9.0
 */
#define LRG_MEMORY_SOURCE_INSTANCE "instance-count"

/**
 * LRG_MEMORY_SOURCE_TRACKED:
 *
 * Value of #LrgMemoryTypeCount.source when the count came from the
 * toggle-ref registry.
 *
 * Since: 0.9.0
 */
#define LRG_MEMORY_SOURCE_TRACKED "tracked"

/**
 * LRG_MEMORY_HISTORY_CAP:
 *
 * Samples lrg_memory_profiler_record_sample() keeps (oldest dropped).
 *
 * Since: 0.9.0
 */
#define LRG_MEMORY_HISTORY_CAP (120)

/**
 * LrgMemoryTypeCount:
 * @type_name: the #GType name
 * @count: live instances
 * @instance_size: bytes of one instance (g_type_query())
 * @bytes: @count times @instance_size
 * @source: %LRG_MEMORY_SOURCE_INSTANCE or %LRG_MEMORY_SOURCE_TRACKED
 *
 * One row of lrg_memory_profiler_get_type_counts().
 *
 * Since: 0.9.0
 */
typedef struct _LrgMemoryTypeCount LrgMemoryTypeCount;

struct _LrgMemoryTypeCount
{
	gchar   *type_name;
	guint64  count;
	guint    instance_size;
	guint64  bytes;
	gchar   *source;
};

LRG_AVAILABLE_IN_ALL
GType lrg_memory_type_count_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MEMORY_TYPE_COUNT (lrg_memory_type_count_get_type ())

LRG_AVAILABLE_IN_ALL
LrgMemoryTypeCount * lrg_memory_type_count_copy (const LrgMemoryTypeCount *count);

LRG_AVAILABLE_IN_ALL
void lrg_memory_type_count_free (LrgMemoryTypeCount *count);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgMemoryTypeCount, lrg_memory_type_count_free)

/**
 * LrgMemoryResource:
 * @name: cache key or asset name
 * @kind: `texture`, `font`, `sound`, `music`, `model`, `animation`,
 *   `material`, `object`, `mesh` or `other`
 * @refs: GObject references, or clip count for an animation set
 * @cpu_bytes: estimated CPU-side bytes
 * @gpu_bytes: estimated VRAM bytes
 * @details: short human-readable note (may be empty)
 *
 * One resident asset, from lrg_memory_profiler_get_resources().
 *
 * Since: 0.9.0
 */
typedef struct _LrgMemoryResource LrgMemoryResource;

struct _LrgMemoryResource
{
	gchar   *name;
	gchar   *kind;
	guint    refs;
	guint64  cpu_bytes;
	guint64  gpu_bytes;
	gchar   *details;
};

LRG_AVAILABLE_IN_ALL
GType lrg_memory_resource_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MEMORY_RESOURCE (lrg_memory_resource_get_type ())

LRG_AVAILABLE_IN_ALL
LrgMemoryResource * lrg_memory_resource_copy (const LrgMemoryResource *resource);

LRG_AVAILABLE_IN_ALL
void lrg_memory_resource_free (LrgMemoryResource *resource);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgMemoryResource, lrg_memory_resource_free)

/**
 * LrgMemoryTotals:
 * @instances: sum of type counts
 * @types: number of type rows
 * @type_bytes: sum of type-row bytes
 * @resources: number of resource rows
 * @cpu_bytes: sum of resource CPU bytes
 * @gpu_bytes: sum of resource GPU bytes
 *
 * Caller-allocated totals. No heap pointers, so no free function.
 *
 * Since: 0.9.0
 */
typedef struct
{
	guint64 instances;
	guint   types;
	guint64 type_bytes;
	guint   resources;
	guint64 cpu_bytes;
	guint64 gpu_bytes;
} LrgMemoryTotals;

/**
 * LrgMemorySnapshotInfo:
 * @id: snapshot id (`m` plus a counter)
 * @label: label passed to lrg_memory_profiler_snapshot(), or empty
 * @time_us: g_get_real_time() when the snapshot was taken
 * @totals: totals at that moment
 *
 * The summary of a stored snapshot. The type and resource rows stay
 * inside the profiler.
 *
 * Since: 0.9.0
 */
typedef struct _LrgMemorySnapshotInfo LrgMemorySnapshotInfo;

struct _LrgMemorySnapshotInfo
{
	gchar          *id;
	gchar          *label;
	gint64          time_us;
	LrgMemoryTotals totals;
};

LRG_AVAILABLE_IN_ALL
GType lrg_memory_snapshot_info_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MEMORY_SNAPSHOT_INFO (lrg_memory_snapshot_info_get_type ())

LRG_AVAILABLE_IN_ALL
LrgMemorySnapshotInfo * lrg_memory_snapshot_info_copy (const LrgMemorySnapshotInfo *info);

LRG_AVAILABLE_IN_ALL
void lrg_memory_snapshot_info_free (LrgMemorySnapshotInfo *info);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgMemorySnapshotInfo, lrg_memory_snapshot_info_free)

/**
 * LrgMemoryTypeDelta:
 * @type_name: the #GType name
 * @before: instance count in the older snapshot
 * @after: instance count in the newer snapshot
 * @delta: @after minus @before
 * @bytes_delta: byte estimate after minus before
 *
 * A type whose count or bytes changed between two snapshots.
 *
 * Since: 0.9.0
 */
typedef struct _LrgMemoryTypeDelta LrgMemoryTypeDelta;

struct _LrgMemoryTypeDelta
{
	gchar   *type_name;
	guint64  before;
	guint64  after;
	gint64   delta;
	gint64   bytes_delta;
};

LRG_AVAILABLE_IN_ALL
GType lrg_memory_type_delta_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MEMORY_TYPE_DELTA (lrg_memory_type_delta_get_type ())

LRG_AVAILABLE_IN_ALL
LrgMemoryTypeDelta * lrg_memory_type_delta_copy (const LrgMemoryTypeDelta *delta);

LRG_AVAILABLE_IN_ALL
void lrg_memory_type_delta_free (LrgMemoryTypeDelta *delta);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgMemoryTypeDelta, lrg_memory_type_delta_free)

/**
 * LrgMemoryResourceDelta:
 * @name: resource name
 * @kind: resource kind
 * @change: `added`, `removed` or `changed`
 * @bytes_delta: CPU plus GPU bytes after minus before; 0 when only @refs changed
 *
 * A resource that appeared, disappeared, or changed size or reference
 * count between two snapshots.
 *
 * Since: 0.9.0
 */
typedef struct _LrgMemoryResourceDelta LrgMemoryResourceDelta;

struct _LrgMemoryResourceDelta
{
	gchar  *name;
	gchar  *kind;
	gchar  *change;
	gint64  bytes_delta;
};

LRG_AVAILABLE_IN_ALL
GType lrg_memory_resource_delta_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_MEMORY_RESOURCE_DELTA (lrg_memory_resource_delta_get_type ())

LRG_AVAILABLE_IN_ALL
LrgMemoryResourceDelta * lrg_memory_resource_delta_copy (const LrgMemoryResourceDelta *delta);

LRG_AVAILABLE_IN_ALL
void lrg_memory_resource_delta_free (LrgMemoryResourceDelta *delta);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgMemoryResourceDelta, lrg_memory_resource_delta_free)

#define LRG_TYPE_MEMORY_PROFILER (lrg_memory_profiler_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMemoryProfiler, lrg_memory_profiler, LRG, MEMORY_PROFILER, GObject)

/**
 * lrg_memory_profiler_instance_counts_available:
 *
 * Whether g_type_get_instance_count() is counting. That is true only
 * when the process environment contained `GOBJECT_DEBUG=instance-count`
 * before GObject initialized the type system. Setting the variable from
 * a library constructor, or from main() after the first #GObject exists,
 * is too late: libgobject reads the variable once, often from its own
 * constructors, which run before main().
 *
 * The editor launcher can append `instance-count` at the top of main(),
 * before any GObject call. Tests and hosts that cannot do that use
 * lrg_memory_profiler_watch_type().
 *
 * Returns: %TRUE when instance counts are live
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_memory_profiler_instance_counts_available (void);

/**
 * lrg_memory_profiler_new:
 *
 * Creates a profiler. Snapshots and the history ring belong to this
 * instance. The toggle-ref registry is process-wide and shared.
 *
 * Returns: (transfer full): a new #LrgMemoryProfiler
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgMemoryProfiler * lrg_memory_profiler_new (void);

/**
 * lrg_memory_profiler_get_default:
 *
 * The process-wide profiler. The editor panel keeps its own instance
 * so its snapshot list is not shared with every caller.
 *
 * Returns: (transfer none): the default profiler
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgMemoryProfiler * lrg_memory_profiler_get_default (void);

/**
 * lrg_memory_profiler_watch_type:
 * @self: an #LrgMemoryProfiler
 * @type: an instantiable #GObject type
 *
 * Counts future instances of @type with a toggle reference. The class
 * constructor is wrapped once for the process. Subclasses are not
 * counted unless they are watched themselves. Types derived from
 * #GInitiallyUnowned are refused: a toggle reference sinks a floating
 * object and would break g_object_ref_sink().
 *
 * Call this before instances of @type are created. Existing instances
 * are not counted; use lrg_memory_profiler_track() for those.
 *
 * Returns: %TRUE if @type is now watched
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_memory_profiler_watch_type (LrgMemoryProfiler *self,
                                         GType              type);

/**
 * lrg_memory_profiler_track:
 * @self: an #LrgMemoryProfiler
 * @object: a #GObject
 *
 * Counts @object until its last user reference drops. Idempotent.
 * Refuses #GInitiallyUnowned derivatives so a floating reference is
 * not sunk.
 *
 * Returns: %TRUE if @object is tracked
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_memory_profiler_track (LrgMemoryProfiler *self,
                                    GObject           *object);

/**
 * lrg_memory_profiler_get_tracked_count:
 * @self: an #LrgMemoryProfiler
 * @type: a #GType
 *
 * Instances of @type currently held by the toggle-ref registry.
 * Independent of g_type_get_instance_count().
 *
 * Returns: the tracked count, or 0
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_memory_profiler_get_tracked_count (LrgMemoryProfiler *self,
                                            GType              type);

/**
 * lrg_memory_profiler_get_tracked_instances:
 * @self: an #LrgMemoryProfiler
 *
 * Sum of every tracked type's count.
 *
 * Returns: the sum, or 0
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gint lrg_memory_profiler_get_tracked_instances (LrgMemoryProfiler *self);

/**
 * lrg_memory_profiler_set_asset_manager:
 * @self: an #LrgMemoryProfiler
 * @manager: (nullable): the manager whose caches are resident, or %NULL
 *
 * Sets the manager lrg_memory_profiler_get_resources() walks, via
 * lrg_asset_manager_foreach_cached().
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_memory_profiler_set_asset_manager (LrgMemoryProfiler *self,
                                            LrgAssetManager   *manager);

/**
 * lrg_memory_profiler_get_asset_manager:
 * @self: an #LrgMemoryProfiler
 *
 * Returns: (transfer none) (nullable): the manager, or %NULL
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
LrgAssetManager * lrg_memory_profiler_get_asset_manager (LrgMemoryProfiler *self);

/**
 * lrg_memory_profiler_estimate_texture:
 * @width: width in pixels; negative returns 0
 * @height: height in pixels; negative returns 0
 * @mipmaps: mip levels (0 is treated as 1, clamped to 16)
 * @format: a #GrlPixelFormat value; 0 returns 0; an unknown positive
 *   format is estimated as 4 bytes per pixel
 *
 * Estimates the byte size of a texture image, including the mip chain.
 * Block-compressed formats round each mip up to whole blocks (DXT1 and
 * the 4 bpp family are 8 bytes per 4x4; DXT3/DXT5 and ASTC 4x4 are 16
 * bytes per 4x4; ASTC 8x8 is 16 bytes per 8x8).
 *
 * Returns: the estimate in bytes
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_memory_profiler_estimate_texture (gint width,
                                              gint height,
                                              gint mipmaps,
                                              gint format);

/**
 * lrg_memory_profiler_estimate_mesh:
 * @vertex_count: vertices; negative returns 0
 * @triangle_count: triangles, used only when @indexed is %TRUE
 * @attrs: which arrays are present
 * @indexed: add @triangle_count * 3 * 2 bytes of 16-bit indices
 *
 * Estimates a mesh from its attribute mask. Positions are 12 bytes,
 * texcoords 8, texcoords2 8, normals 12, tangents 16, colors 4, bones
 * 20 (4 index bytes + 4 float weights) and anim 24 (positions + normals).
 *
 * Returns: the estimate in bytes
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_memory_profiler_estimate_mesh (gint               vertex_count,
                                           gint               triangle_count,
                                           LrgMemoryMeshAttrs attrs,
                                           gboolean           indexed);

/**
 * lrg_memory_profiler_estimate_pcm:
 * @frames: sample frames; negative returns 0
 * @bits: bits per sample; not a positive multiple of 8 returns 0
 * @channels: channel count; not positive returns 0
 *
 * Estimates interleaved PCM: frames * channels * (bits / 8). Graylib
 * does not expose a #GrlSound PCM size, so resident sounds report 0
 * bytes and this function is the formula a host can apply itself.
 *
 * Returns: the estimate in bytes
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_memory_profiler_estimate_pcm (gint64 frames,
                                          gint   bits,
                                          gint   channels);

/**
 * lrg_memory_profiler_get_type_counts:
 * @self: an #LrgMemoryProfiler
 *
 * When instance counts are available, every instantiable #GObject type
 * with a non-zero count, source %LRG_MEMORY_SOURCE_INSTANCE. Otherwise
 * every toggle-tracked type with a non-zero count, source
 * %LRG_MEMORY_SOURCE_TRACKED. A type is never listed twice. Rows are
 * sorted by type name.
 *
 * Returns: (transfer full) (element-type LrgMemoryTypeCount): the rows
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_memory_profiler_get_type_counts (LrgMemoryProfiler *self);

/**
 * lrg_memory_profiler_get_resources:
 * @self: an #LrgMemoryProfiler
 *
 * Resident assets of the profiler's #LrgAssetManager, via
 * lrg_asset_manager_foreach_cached(). Model meshes and material-map
 * textures are folded into the model's byte counts (not emitted again
 * as texture rows). Sounds report 0 bytes because the PCM buffer is
 * private to graylib. Music is streamed and reports 0 bytes.
 *
 * Returns: (transfer full) (element-type LrgMemoryResource): the rows
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_memory_profiler_get_resources (LrgMemoryProfiler *self);

/**
 * lrg_memory_profiler_get_totals:
 * @self: an #LrgMemoryProfiler
 * @totals: (out caller-allocates): filled totals
 *
 * Sums lrg_memory_profiler_get_type_counts() and
 * lrg_memory_profiler_get_resources().
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_memory_profiler_get_totals (LrgMemoryProfiler *self,
                                     LrgMemoryTotals   *totals);

/**
 * lrg_memory_profiler_snapshot:
 * @self: an #LrgMemoryProfiler
 * @label: (nullable): a label stored with the snapshot
 *
 * Stores the current type counts, resources and totals. The id is
 * `m` plus a per-profiler counter (`m1`, `m2`, ...). @time is
 * g_get_real_time() in microseconds.
 *
 * Returns: (transfer full): the new snapshot id
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_memory_profiler_snapshot (LrgMemoryProfiler *self,
                                      const gchar       *label);

/**
 * lrg_memory_profiler_list_snapshots:
 * @self: an #LrgMemoryProfiler
 *
 * Returns: (transfer full) (element-type LrgMemorySnapshotInfo): stored
 *   snapshots, oldest first
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_memory_profiler_list_snapshots (LrgMemoryProfiler *self);

/**
 * lrg_memory_profiler_clear_snapshots:
 * @self: an #LrgMemoryProfiler
 *
 * Drops every stored snapshot.
 *
 * Returns: how many were removed
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
guint lrg_memory_profiler_clear_snapshots (LrgMemoryProfiler *self);

/**
 * lrg_memory_profiler_diff:
 * @self: an #LrgMemoryProfiler
 * @from_id: the older snapshot id
 * @to_id: (nullable): the newer snapshot id, or %NULL for a fresh live
 *   snapshot that is not stored
 * @type_deltas: (out) (transfer full) (element-type LrgMemoryTypeDelta) (nullable):
 *   type rows whose count or bytes differ; zero deltas are omitted
 * @resource_deltas: (out) (transfer full) (element-type LrgMemoryResourceDelta) (nullable):
 *   resource rows keyed by kind and name; a refs-only change is
 *   `changed` with a zero @bytes_delta
 * @error: (nullable): return location for a #GError
 *
 * Unknown ids set %G_IO_ERROR_NOT_FOUND. A missing @from_id sets
 * %G_IO_ERROR_INVALID_ARGUMENT.
 *
 * Returns: %TRUE on success
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_memory_profiler_diff (LrgMemoryProfiler  *self,
                                   const gchar        *from_id,
                                   const gchar        *to_id,
                                   GPtrArray         **type_deltas,
                                   GPtrArray         **resource_deltas,
                                   GError            **error);

/**
 * lrg_memory_profiler_record_sample:
 * @self: an #LrgMemoryProfiler
 *
 * Appends the current totals and per-type counts to the history ring
 * (%LRG_MEMORY_HISTORY_CAP samples, oldest dropped).
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_memory_profiler_record_sample (LrgMemoryProfiler *self);

/**
 * lrg_memory_profiler_get_history:
 * @self: an #LrgMemoryProfiler
 * @type_name: (nullable): a type whose counts are also returned
 * @max_samples: how many of the newest samples to return; 0 means all
 * @instances: (out) (transfer full) (array length=n_samples) (nullable):
 *   instance totals, oldest first
 * @cpu_bytes: (out) (transfer full) (array length=n_samples) (nullable):
 *   CPU byte totals
 * @gpu_bytes: (out) (transfer full) (array length=n_samples) (nullable):
 *   GPU byte totals
 * @type_counts: (out) (transfer full) (array length=n_samples) (nullable):
 *   counts of @type_name (0 when the type was absent that sample);
 *   left %NULL when @type_name is %NULL
 * @n_samples: (out): the number of samples written
 *
 * Since: 0.9.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_memory_profiler_get_history (LrgMemoryProfiler *self,
                                      const gchar       *type_name,
                                      guint              max_samples,
                                      guint64          **instances,
                                      guint64          **cpu_bytes,
                                      guint64          **gpu_bytes,
                                      guint64          **type_counts,
                                      guint             *n_samples);

G_END_DECLS
