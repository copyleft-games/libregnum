/* test-memory-profiler.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Toggle-ref counts, size estimates, snapshots and asset residency.
 * Instance counts from GOBJECT_DEBUG are optional: the assertions that
 * need them run only when lrg_memory_profiler_instance_counts_available()
 * is true, and the tracked-registry assertions run only when it is not.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

#define MEM_TYPE_PROBE (mem_probe_get_type ())
G_DECLARE_FINAL_TYPE (MemProbe, mem_probe, MEM, PROBE, GObject)

struct _MemProbe
{
	GObject parent_instance;
};

G_DEFINE_TYPE (MemProbe, mem_probe, G_TYPE_OBJECT)

static void
mem_probe_class_init (MemProbeClass *klass)
{
}

static void
mem_probe_init (MemProbe *self)
{
}

#define MEM_TYPE_OTHER (mem_other_get_type ())
G_DECLARE_FINAL_TYPE (MemOther, mem_other, MEM, OTHER, GObject)

struct _MemOther
{
	GObject parent_instance;
};

G_DEFINE_TYPE (MemOther, mem_other, G_TYPE_OBJECT)

static void
mem_other_class_init (MemOtherClass *klass)
{
}

static void
mem_other_init (MemOther *self)
{
}

#define MEM_TYPE_FLOAT (mem_float_get_type ())
G_DECLARE_FINAL_TYPE (MemFloat, mem_float, MEM, FLOAT, GInitiallyUnowned)

struct _MemFloat
{
	GInitiallyUnowned parent_instance;
};

G_DEFINE_TYPE (MemFloat, mem_float, G_TYPE_INITIALLY_UNOWNED)

static void
mem_float_class_init (MemFloatClass *klass)
{
}

static void
mem_float_init (MemFloat *self)
{
}

static void
test_estimates (void)
{
	g_assert_cmpuint (lrg_memory_profiler_estimate_texture (64, 64, 1, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8),
	                  ==, 16384);
	g_assert_cmpuint (lrg_memory_profiler_estimate_texture (64, 64, 2, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8),
	                  ==, 20480);
	g_assert_cmpuint (lrg_memory_profiler_estimate_texture (4, 4, 1, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGB),
	                  ==, 8);
	g_assert_cmpuint (lrg_memory_profiler_estimate_texture (8, 8, 1, GRL_PIXELFORMAT_COMPRESSED_DXT1_RGBA),
	                  ==, 32);
	g_assert_cmpuint (lrg_memory_profiler_estimate_texture (8, 8, 1, GRL_PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA),
	                  ==, 16);
	g_assert_cmpuint (lrg_memory_profiler_estimate_texture (0, 64, 1, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8),
	                  ==, 0);
	g_assert_cmpuint (lrg_memory_profiler_estimate_texture (-1, 8, 1, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8),
	                  ==, 0);
	g_assert_cmpuint (lrg_memory_profiler_estimate_texture (8, 8, -1, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8),
	                  ==, 0);
	g_assert_cmpuint (lrg_memory_profiler_estimate_texture (8, 8, 1, 0), ==, 0);

	g_assert_cmpuint (lrg_memory_profiler_estimate_mesh (3, 0, LRG_MEMORY_MESH_ATTR_POSITIONS, FALSE), ==, 36);
	g_assert_cmpuint (lrg_memory_profiler_estimate_mesh (3, 1, LRG_MEMORY_MESH_ATTR_POSITIONS, TRUE), ==, 42);
	g_assert_cmpuint (lrg_memory_profiler_estimate_mesh (-1, 0, LRG_MEMORY_MESH_ATTR_POSITIONS, FALSE), ==, 0);

	g_assert_cmpuint (lrg_memory_profiler_estimate_pcm (100, 16, 2), ==, 400);
	g_assert_cmpuint (lrg_memory_profiler_estimate_pcm (-1, 16, 2), ==, 0);
	g_assert_cmpuint (lrg_memory_profiler_estimate_pcm (10, 12, 1), ==, 0);
	g_assert_cmpuint (lrg_memory_profiler_estimate_pcm (10, 16, 0), ==, 0);
}

static void
test_tracked (void)
{
	g_autoptr(LrgMemoryProfiler) profiler = lrg_memory_profiler_new ();
	MemProbe *probe_a;
	MemProbe *probe_b;
	MemOther *other;
	MemFloat *floating;

	/* The toggle-ref path is what tests use when GOBJECT_DEBUG is unset. */
	if (lrg_memory_profiler_instance_counts_available ())
	{
		g_test_message ("instance-count is live; tracked rows are hidden from get_type_counts()");
		g_assert_cmpint (g_type_get_instance_count (MEM_TYPE_PROBE), >=, 0);
	}

	g_assert_true (lrg_memory_profiler_watch_type (profiler, MEM_TYPE_PROBE));
	g_assert_false (lrg_memory_profiler_watch_type (profiler, MEM_TYPE_FLOAT));
	g_assert_false (lrg_memory_profiler_watch_type (profiler, G_TYPE_INVALID));

	probe_a = g_object_new (MEM_TYPE_PROBE, NULL);
	probe_b = g_object_new (MEM_TYPE_PROBE, NULL);
	g_assert_cmpint (lrg_memory_profiler_get_tracked_count (profiler, MEM_TYPE_PROBE), ==, 2);
	g_object_unref (probe_a);
	g_assert_cmpint (lrg_memory_profiler_get_tracked_count (profiler, MEM_TYPE_PROBE), ==, 1);
	g_assert_true (lrg_memory_profiler_track (profiler, G_OBJECT (probe_b)));
	g_assert_cmpint (lrg_memory_profiler_get_tracked_count (profiler, MEM_TYPE_PROBE), ==, 1);
	g_object_unref (probe_b);
	g_assert_cmpint (lrg_memory_profiler_get_tracked_count (profiler, MEM_TYPE_PROBE), ==, 0);

	g_assert_cmpint (lrg_memory_profiler_get_tracked_count (profiler, MEM_TYPE_OTHER), ==, 0);
	other = g_object_new (MEM_TYPE_OTHER, NULL);
	g_assert_true (lrg_memory_profiler_track (profiler, G_OBJECT (other)));
	g_assert_true (lrg_memory_profiler_track (profiler, G_OBJECT (other)));
	g_assert_cmpint (lrg_memory_profiler_get_tracked_count (profiler, MEM_TYPE_OTHER), ==, 1);
	g_assert_cmpint (lrg_memory_profiler_get_tracked_instances (profiler), >=, 1);
	g_object_unref (other);
	g_assert_cmpint (lrg_memory_profiler_get_tracked_count (profiler, MEM_TYPE_OTHER), ==, 0);

	floating = g_object_new (MEM_TYPE_FLOAT, NULL);
	g_assert_false (lrg_memory_profiler_track (profiler, G_OBJECT (floating)));
	g_object_ref_sink (floating);
	g_object_unref (floating);
}

static gboolean
delta_has_type (GPtrArray   *deltas,
                const gchar *type_name,
                gint64       min_delta)
{
	guint i;

	for (i = 0; i < deltas->len; i++)
	{
		LrgMemoryTypeDelta *delta = g_ptr_array_index (deltas, i);

		if (g_strcmp0 (delta->type_name, type_name) == 0 && delta->delta >= min_delta)
			return TRUE;
	}
	return FALSE;
}

static gboolean
resource_added (GPtrArray   *deltas,
                const gchar *kind)
{
	guint i;

	for (i = 0; i < deltas->len; i++)
	{
		LrgMemoryResourceDelta *delta = g_ptr_array_index (deltas, i);

		if (g_strcmp0 (delta->kind, kind) == 0 && g_strcmp0 (delta->change, "added") == 0)
			return TRUE;
	}
	return FALSE;
}

static void
test_snapshots (void)
{
	g_autoptr(LrgMemoryProfiler) profiler = lrg_memory_profiler_new ();
	g_autoptr(GPtrArray)         types = NULL;
	g_autoptr(GPtrArray)         resources = NULL;
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *first = NULL;
	g_autofree gchar            *second = NULL;
	g_autofree guint64          *instances = NULL;
	MemProbe                    *probe;
	guint                        n = 0;
	guint                        removed;

	g_assert_true (lrg_memory_profiler_watch_type (profiler, MEM_TYPE_PROBE));
	first = lrg_memory_profiler_snapshot (profiler, "before");
	g_assert_true (g_str_has_prefix (first, "m"));
	probe = g_object_new (MEM_TYPE_PROBE, NULL);
	second = lrg_memory_profiler_snapshot (profiler, "after");
	g_assert_true (lrg_memory_profiler_diff (profiler, first, second, &types, &resources, &error));
	g_assert_no_error (error);
	if (!lrg_memory_profiler_instance_counts_available ())
		g_assert_true (delta_has_type (types, g_type_name (MEM_TYPE_PROBE), 1));
	g_object_unref (probe);

	g_assert_false (lrg_memory_profiler_diff (profiler, "missing", NULL, NULL, NULL, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);
	g_assert_false (lrg_memory_profiler_diff (profiler, NULL, NULL, NULL, NULL, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);
	g_assert_false (lrg_memory_profiler_diff (profiler, first, "live", NULL, NULL, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);

	lrg_memory_profiler_record_sample (profiler);
	lrg_memory_profiler_record_sample (profiler);
	lrg_memory_profiler_get_history (profiler, g_type_name (MEM_TYPE_PROBE), 0,
	                                 &instances, NULL, NULL, NULL, &n);
	g_assert_cmpuint (n, >=, 2);
	g_assert_nonnull (instances);

	removed = lrg_memory_profiler_clear_snapshots (profiler);
	g_assert_cmpuint (removed, >=, 2);
	{
		g_autoptr(GPtrArray) left = lrg_memory_profiler_list_snapshots (profiler);

		g_assert_cmpuint (left->len, ==, 0);
	}
}

static void
count_objects (const gchar *name,
               const gchar *kind,
               GObject     *asset,
               guint        count,
               gpointer     user_data)
{
	guint *n = user_data;

	if (g_strcmp0 (kind, "object") == 0 && asset != NULL && count == 1)
		(*n)++;
}

static void
rm_rf (const gchar *path)
{
	g_autoptr(GDir) d = g_dir_open (path, 0, NULL);
	const gchar    *name;

	while (d != NULL && (name = g_dir_read_name (d)) != NULL)
	{
		g_autofree gchar *child = g_build_filename (path, name, NULL);

		if (g_file_test (child, G_FILE_TEST_IS_DIR))
			rm_rf (child);
		else
			g_unlink (child);
	}
	g_rmdir (path);
}

static gboolean resource_added_kind_live (GPtrArray *rows);

static void
test_assets (void)
{
	g_autoptr(LrgMemoryProfiler) profiler = lrg_memory_profiler_new ();
	g_autoptr(LrgAssetManager)   manager = lrg_asset_manager_new ();
	g_autoptr(LrgRegistry)       registry = lrg_registry_new ();
	g_autoptr(LrgDataLoader)     loader = lrg_data_loader_new ();
	g_autoptr(GPtrArray)         empty = NULL;
	g_autoptr(GPtrArray)         loaded = NULL;
	g_autoptr(GPtrArray)         types = NULL;
	g_autoptr(GPtrArray)         resources = NULL;
	g_autoptr(GError)            error = NULL;
	g_autofree gchar            *dir = g_dir_make_tmp ("lrg-memnet-XXXXXX", NULL);
	g_autofree gchar            *path = NULL;
	g_autofree gchar            *before = NULL;
	g_autofree gchar            *after = NULL;
	guint                        seen = 0;
	const gchar                 *yaml = "type: item-def\nvalue: 1\n";

	g_assert_nonnull (dir);
	lrg_memory_profiler_set_asset_manager (profiler, manager);
	g_assert_true (lrg_memory_profiler_get_asset_manager (profiler) == manager);
	empty = lrg_memory_profiler_get_resources (profiler);
	g_assert_cmpuint (empty->len, ==, 0);
	lrg_asset_manager_foreach_cached (manager, count_objects, &seen);
	g_assert_cmpuint (seen, ==, 0);

	lrg_registry_register_builtin (registry);
	lrg_data_loader_set_registry (loader, registry);
	lrg_asset_manager_set_data_loader (manager, loader);
	lrg_asset_manager_add_search_path (manager, dir);
	path = g_build_filename (dir, "item.yaml", NULL);
	g_assert_true (g_file_set_contents (path, yaml, -1, &error));
	g_assert_no_error (error);
	before = lrg_memory_profiler_snapshot (profiler, "empty");
	g_assert_nonnull (lrg_asset_manager_load_object (manager, "item.yaml", &error));
	g_assert_no_error (error);
	loaded = lrg_memory_profiler_get_resources (profiler);
	g_assert_true (resource_added_kind_live (loaded));
	after = lrg_memory_profiler_snapshot (profiler, "loaded");
	g_assert_true (lrg_memory_profiler_diff (profiler, before, after, &types, &resources, &error));
	g_assert_no_error (error);
	g_assert_true (resource_added (resources, "object"));
	rm_rf (dir);
}

/* resource_added() is the diff form. Live rows use LrgMemoryResource. */
static gboolean
resource_added_kind_live (GPtrArray *rows)
{
	guint i;

	for (i = 0; i < rows->len; i++)
	{
		LrgMemoryResource *row = g_ptr_array_index (rows, i);

		if (g_strcmp0 (row->kind, "object") == 0)
			return TRUE;
	}
	return FALSE;
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/memory-profiler/estimates", test_estimates);
	g_test_add_func ("/memory-profiler/tracked", test_tracked);
	g_test_add_func ("/memory-profiler/snapshots", test_snapshots);
	g_test_add_func ("/memory-profiler/assets", test_assets);
	return g_test_run ();
}
