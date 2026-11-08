/* test-profiler-capture.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Hierarchical profiler: disabled early-out, nested sections, a second
 * thread lane, counters, .rprof byte stability, GPU events without a
 * window, and a pinned-CPU overhead measurement.
 */

#include <errno.h>
#include <sched.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>

/* ==========================================================================
 * Hierarchy
 * ========================================================================== */

typedef struct
{
	LrgProfiler *profiler;
	GMutex       mutex;
	GCond        cond;
	gboolean     go;
	gboolean     done;
} Worker;

static gpointer
worker_thread (gpointer data)
{
	Worker *w = (Worker *) data;

	g_mutex_lock (&w->mutex);
	while (!w->go)
		g_cond_wait (&w->cond, &w->mutex);
	g_mutex_unlock (&w->mutex);

	lrg_profiler_set_thread_name (w->profiler, "worker");
	lrg_profiler_begin_section (w->profiler, "worker.job");
	lrg_profiler_end_section (w->profiler, "worker.job");

	g_mutex_lock (&w->mutex);
	w->done = TRUE;
	g_cond_signal (&w->cond);
	g_mutex_unlock (&w->mutex);
	return NULL;
}

static void
test_disabled (void)
{
	g_autoptr(LrgProfiler) profiler = lrg_profiler_new ();

	g_assert_false (lrg_profiler_is_enabled (profiler));
	lrg_profiler_begin_section (profiler, "should.not.record");
	lrg_profiler_end_section (profiler, "should.not.record");
	lrg_profiler_begin_frame (profiler);
	lrg_profiler_end_frame (profiler);
	g_assert_cmpuint (lrg_profiler_get_sample_count (profiler, "should.not.record"), ==, 0);
	g_assert_cmpuint (lrg_profiler_get_n_frames (profiler), ==, 0);
	g_assert_null (lrg_profiler_get_gpu_timer (profiler));
}

static void
test_hierarchy (void)
{
	g_autoptr(LrgProfiler) profiler = lrg_profiler_new ();
	g_autoptr(LrgProfilerCapture) cap = NULL;
	g_autoptr(GList) names = NULL;
	g_auto(GStrv) sections = NULL;
	const LrgProfilerEvent *events;
	const LrgProfilerCounter *counters;
	Worker     w;
	GThread   *thread;
	guint      n_events = 0;
	guint      n_counters = 0;
	guint      calls = 0;
	guint      i;
	gint64     outer;
	gint64     inner;
	gboolean   saw_worker = FALSE;

	memset (&w, 0, sizeof w);
	w.profiler = profiler;
	g_mutex_init (&w.mutex);
	g_cond_init (&w.cond);
	thread = g_thread_new ("prof-worker", worker_thread, &w);

	lrg_profiler_set_enabled (profiler, TRUE);
	lrg_profiler_set_thread_name (profiler, "main");
	lrg_profiler_begin_frame (profiler);
	g_assert_cmpuint (lrg_profiler_get_frame_index (profiler), ==, 0);

	lrg_profiler_begin_section (profiler, "frame");
	lrg_profiler_begin_section (profiler, "sim");
	g_assert_true (lrg_profiler_is_section_active (profiler, "sim"));
	g_usleep (2000);
	lrg_profiler_end_section (profiler, "sim");
	lrg_profiler_set_counter (profiler, "draws", 3.0);

	g_mutex_lock (&w.mutex);
	w.go = TRUE;
	g_cond_signal (&w.cond);
	while (!w.done)
		g_cond_wait (&w.cond, &w.mutex);
	g_mutex_unlock (&w.mutex);
	g_thread_join (thread);

	lrg_profiler_end_section (profiler, "frame");
	lrg_profiler_end_frame (profiler);
	g_assert_cmpuint (lrg_profiler_get_n_frames (profiler), ==, 1);
	g_assert_cmpuint (lrg_profiler_get_sample_count (profiler, "sim"), ==, 1);
	g_assert_cmpuint (lrg_profiler_get_sample_count (profiler, "frame"), ==, 1);
	g_assert_cmpfloat (lrg_profiler_get_max_ms (profiler, "frame"), >=, lrg_profiler_get_min_ms (profiler, "sim"));

	names = lrg_profiler_get_section_names (profiler);
	g_assert_nonnull (g_list_find_custom (names, "frame", (GCompareFunc) g_strcmp0));

	cap = lrg_profiler_capture (profiler, 0);
	g_assert_cmpuint (lrg_profiler_capture_get_n_frames (cap), ==, 1);
	g_assert_cmpint (lrg_profiler_capture_find_frame (cap, 0), ==, 0);
	g_assert_cmpuint (lrg_profiler_capture_get_n_threads (cap), >=, 2);

	for (i = 0; i < lrg_profiler_capture_get_n_threads (cap); i++)
	{
		if (g_strcmp0 (lrg_profiler_capture_get_thread_name (cap, i), "worker") == 0)
			saw_worker = TRUE;
	}
	g_assert_true (saw_worker);

	events = lrg_profiler_capture_get_events (cap, 0, &n_events);
	g_assert_cmpuint (n_events, >=, 3);
	g_assert_nonnull (events);
	outer = lrg_profiler_capture_get_section_total_us (cap, 0, "frame", &calls);
	g_assert_cmpuint (calls, ==, 1);
	inner = lrg_profiler_capture_get_section_total_us (cap, 0, "sim", &calls);
	g_assert_cmpuint (calls, ==, 1);
	g_assert_cmpint (outer, >=, inner);
	g_assert_cmpint (lrg_profiler_capture_get_section_total_us (cap, 0, "worker.job", &calls), >=, 0);
	g_assert_cmpuint (calls, ==, 1);

	counters = lrg_profiler_capture_get_counters (cap, 0, &n_counters);
	g_assert_cmpuint (n_counters, ==, 1);
	g_assert_cmpstr (counters[0].name, ==, "draws");
	g_assert_cmpfloat (counters[0].value, ==, 3.0);

	sections = lrg_profiler_capture_get_section_names (cap, 0);
	g_assert_nonnull (sections);
	g_assert_cmpint (lrg_profiler_capture_get_frame_gpu_us (cap, 0), ==, -1);

	g_mutex_clear (&w.mutex);
	g_cond_clear (&w.cond);
}

static void
test_rprof_bytes (void)
{
	g_autoptr(LrgProfiler) profiler = lrg_profiler_new ();
	g_autoptr(LrgProfilerCapture) cap = NULL;
	g_autoptr(LrgProfilerCapture) loaded = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = g_dir_make_tmp ("lrg-rprof-XXXXXX", NULL);
	g_autofree gchar *path = NULL;
	g_autofree gchar *a = NULL;
	g_autofree gchar *b = NULL;
	g_autofree gchar *file_bytes = NULL;
	g_autofree gchar *again = NULL;
	gsize la = 0;
	gsize lb = 0;
	gsize lf = 0;
	gsize lg = 0;

	lrg_profiler_set_enabled (profiler, TRUE);
	lrg_profiler_begin_frame (profiler);
	lrg_profiler_begin_section (profiler, "io");
	lrg_profiler_end_section (profiler, "io");
	lrg_profiler_end_frame (profiler);
	cap = lrg_profiler_capture (profiler, 1);

	a = lrg_profiler_capture_save_to_data (cap, &la);
	b = lrg_profiler_capture_save_to_data (cap, &lb);
	g_assert_cmpuint (la, >, 0);
	g_assert_cmpuint (la, ==, lb);
	g_assert_cmpint (memcmp (a, b, la), ==, 0);
	g_assert_nonnull (strstr (a, "format: rprof\n"));

	path = g_build_filename (dir, "cap.rprof", NULL);
	g_assert_true (lrg_profiler_capture_save_to_file (cap, path, &error));
	g_assert_no_error (error);
	loaded = lrg_profiler_capture_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);
	again = lrg_profiler_capture_save_to_data (loaded, &lg);
	g_file_get_contents (path, &file_bytes, &lf, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lf, ==, lg);
	g_assert_cmpint (memcmp (file_bytes, again, lf), ==, 0);
	g_assert_cmpuint (lrg_profiler_capture_get_n_frames (loaded), ==, 1);

	g_assert_null (lrg_profiler_capture_new_from_data ("format: nope\n", -1, &error));
	g_assert_error (error, LRG_PROFILER_ERROR, LRG_PROFILER_ERROR_INVALID);
	g_clear_error (&error);
	g_assert_null (lrg_profiler_capture_new_from_data ("[", -1, &error));
	g_assert_nonnull (error);

	g_unlink (path);
	g_rmdir (dir);
}

static void
test_gpu_event_headless (void)
{
	g_autoptr(LrgProfiler) profiler = lrg_profiler_new ();
	g_autoptr(LrgProfilerCapture) cap = NULL;
	g_autoptr(LrgGpuTimer) timer = lrg_gpu_timer_new ();
	const LrgProfilerEvent *gpu;
	guint    n = 0;
	guint64  index;
	gint64   now;

	g_assert_false (lrg_gpu_timer_is_supported (timer));
	lrg_gpu_timer_begin_frame (timer, 0);
	lrg_gpu_timer_begin (timer, "x");
	lrg_gpu_timer_end (timer);
	lrg_gpu_timer_end_frame (timer);
	g_assert_cmpuint (lrg_gpu_timer_collect (timer, NULL, NULL), ==, 0);
	g_assert_cmpint (lrg_gpu_timer_get_last_frame_us (timer), ==, -1);
	g_assert_cmpuint (lrg_gpu_timer_get_dropped (timer), ==, 0);
	lrg_gpu_timer_release (timer);

	lrg_profiler_set_enabled (profiler, TRUE);
	lrg_profiler_set_gpu_enabled (profiler, TRUE);
	g_assert_nonnull (lrg_profiler_get_gpu_timer (profiler));
	g_assert_false (lrg_gpu_timer_is_supported (lrg_profiler_get_gpu_timer (profiler)));

	lrg_profiler_begin_frame (profiler);
	index = lrg_profiler_get_frame_index (profiler);
	lrg_profiler_begin_gpu_section (profiler, "gpu.opaque");
	lrg_profiler_end_gpu_section (profiler);
	lrg_profiler_end_frame (profiler);

	now = g_get_monotonic_time ();
	g_assert_true (lrg_profiler_add_gpu_event (profiler, index, "gpu.opaque", 0, now, 120));
	cap = lrg_profiler_capture (profiler, 1);
	g_assert_cmpint (lrg_profiler_capture_get_frame_gpu_us (cap, 0), >=, 120);
	gpu = lrg_profiler_capture_get_gpu_events (cap, 0, &n);
	g_assert_cmpuint (n, ==, 1);
	g_assert_cmpstr (gpu[0].name, ==, "gpu.opaque");
	g_assert_cmpuint (gpu[0].thread, ==, LRG_PROFILER_GPU_THREAD);
	g_assert_cmpstr (lrg_profiler_capture_get_thread_name (cap, LRG_PROFILER_GPU_THREAD), ==, "GPU");
}

static void
test_overhead (void)
{
	g_autoptr(LrgProfiler) profiler = lrg_profiler_new ();
	cpu_set_t set;
	gint64    t0;
	gint64    t1;
	guint     i;
	gdouble   disabled_ns;
	gdouble   enabled_ns;
	const guint n_off = 2000000;
	const guint n_on = 8000;
	int       pinned = 0;

	CPU_ZERO (&set);
	CPU_SET (0, &set);
	if (sched_setaffinity (0, sizeof set, &set) != 0)
		g_print ("PROF_OVERHEAD affinity_failed errno=%d\n", errno);
	else
		pinned = 1;

	t0 = g_get_monotonic_time ();
	for (i = 0; i < n_off; i++)
	{
		lrg_profiler_begin_section (profiler, "off");
		lrg_profiler_end_section (profiler, "off");
	}
	t1 = g_get_monotonic_time ();
	disabled_ns = ((gdouble) (t1 - t0) * 1000.0) / (gdouble) n_off;
	g_assert_cmpuint (lrg_profiler_get_sample_count (profiler, "off"), ==, 0);

	lrg_profiler_set_enabled (profiler, TRUE);
	lrg_profiler_begin_frame (profiler);
	for (i = 0; i < 64; i++)
	{
		lrg_profiler_begin_section (profiler, "warm");
		lrg_profiler_end_section (profiler, "warm");
	}
	t0 = g_get_monotonic_time ();
	for (i = 0; i < n_on; i++)
	{
		lrg_profiler_begin_section (profiler, "on.outer");
		lrg_profiler_begin_section (profiler, "on.inner");
		lrg_profiler_end_section (profiler, "on.inner");
		lrg_profiler_end_section (profiler, "on.outer");
	}
	t1 = g_get_monotonic_time ();
	lrg_profiler_end_frame (profiler);
	enabled_ns = ((gdouble) (t1 - t0) * 1000.0) / (gdouble) n_on;

	g_print ("PROF_OVERHEAD cpu=%d pinned=%d disabled_ns_per_pair=%.2f enabled_nested_ns_per_pair=%.2f\n",
	         sched_getcpu (), pinned, disabled_ns, enabled_ns);
	g_test_message ("disabled %.2f ns/pair, enabled nested %.2f ns/pair, cpu %d pinned %d",
	                disabled_ns, enabled_ns, sched_getcpu (), pinned);
	g_assert_cmpfloat (disabled_ns, <, enabled_ns);
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/profiler/capture/disabled", test_disabled);
	g_test_add_func ("/profiler/capture/hierarchy", test_hierarchy);
	g_test_add_func ("/profiler/capture/rprof", test_rprof_bytes);
	g_test_add_func ("/profiler/capture/gpu-event", test_gpu_event_headless);
	g_test_add_func ("/profiler/capture/overhead", test_overhead);
	return g_test_run ();
}
