/* test-frame-capture.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Draw-call capture without GL, .rframe byte stability, scrub gating,
 * a headless world renderer recording the list, and (under xrun.sh) a
 * replay into a render target.
 */

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <raylib.h>
#include <libregnum.h>

static void
fill_call (LrgFrameCall    *call,
           const gchar     *pass,
           LrgFrameCallKind kind,
           const gchar     *mesh,
           guint            triangles)
{
	guint i;

	memset (call, 0, sizeof *call);
	call->pass = pass;
	call->kind = kind;
	call->mesh = mesh;
	call->shader = "lit";
	call->triangles = triangles;
	call->vertices = triangles * 3;
	call->instances = 1;
	call->color[0] = 0.1f;
	call->color[1] = 0.2f;
	call->color[2] = 0.3f;
	call->color[3] = 1.0f;
	for (i = 0; i < 16; i++)
		call->transform[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	call->depth_test = TRUE;
	call->depth_write = TRUE;
	call->cull = TRUE;
}

static void
test_record_and_scrub (void)
{
	g_autoptr(LrgFrameCapture) cap = lrg_frame_capture_new ();
	g_autoptr(GError) error = NULL;
	g_auto(GStrv) passes = NULL;
	LrgFrameCall         a;
	LrgFrameCall         b;
	LrgFrameCallKind     kind = LRG_FRAME_CALL_KIND_MESH;
	const LrgFrameCall  *got;
	gint                 w = 0;
	gint                 h = 0;
	guint                n;

	g_assert_true (lrg_frame_capture_get_default () == lrg_frame_capture_get_default ());
	g_assert_cmpuint (lrg_frame_call_kind_get_type (), !=, G_TYPE_INVALID);
	g_assert_true (lrg_frame_call_kind_from_nick (lrg_frame_call_kind_to_nick (LRG_FRAME_CALL_KIND_VOLUMETRIC), &kind));
	g_assert_cmpint (kind, ==, LRG_FRAME_CALL_KIND_VOLUMETRIC);

	g_assert_true (lrg_frame_capture_note (cap, NULL));
	g_assert_cmpuint (lrg_frame_capture_get_n_calls (cap), ==, 0);
	g_assert_false (lrg_frame_capture_is_active (cap));

	fill_call (&a, "clear", LRG_FRAME_CALL_KIND_CLEAR, NULL, 0);
	fill_call (&b, "opaque", LRG_FRAME_CALL_KIND_PRIMITIVE, "cube", 12);
	b.entity = "Crate";

	lrg_frame_capture_arm (cap);
	g_assert_true (lrg_frame_capture_is_armed (cap));
	lrg_frame_capture_begin_frame (cap, 320, 180);
	g_assert_true (lrg_frame_capture_is_recording (cap));
	g_assert_true (lrg_frame_capture_note (cap, &a));
	g_assert_true (lrg_frame_capture_note (cap, &b));
	lrg_frame_capture_end_frame (cap);

	g_assert_true (lrg_frame_capture_is_captured (cap));
	g_assert_false (lrg_frame_capture_is_recording (cap));
	g_assert_cmpuint (lrg_frame_capture_get_n_calls (cap), ==, 2);
	g_assert_cmpint (lrg_frame_capture_get_width (cap), ==, 320);
	g_assert_cmpint (lrg_frame_capture_get_height (cap), ==, 180);
	got = lrg_frame_capture_get_call (cap, 1);
	g_assert_nonnull (got);
	g_assert_cmpstr (got->pass, ==, "opaque");
	g_assert_cmpint (got->kind, ==, LRG_FRAME_CALL_KIND_PRIMITIVE);
	g_assert_cmpstr (got->entity, ==, "Crate");
	g_assert_cmpstr (got->mesh, ==, "cube");
	g_assert_cmpuint (lrg_frame_capture_count_pass (cap, "opaque"), ==, 1);
	passes = lrg_frame_capture_get_passes (cap);
	g_assert_cmpstr (passes[0], ==, "clear");
	g_assert_cmpstr (passes[1], ==, "opaque");

	/* Scrub keeps the list and gates the next frame's draws. */
	n = lrg_frame_capture_get_n_calls (cap);
	lrg_frame_capture_set_scrub (cap, 0);
	lrg_frame_capture_begin_frame (cap, 320, 180);
	g_assert_true (lrg_frame_capture_is_replaying (cap));
	g_assert_true (lrg_frame_capture_note (cap, &a));
	g_assert_false (lrg_frame_capture_note (cap, &b));
	lrg_frame_capture_end_frame (cap);
	g_assert_cmpuint (lrg_frame_capture_get_n_calls (cap), ==, n);
	g_assert_false (lrg_frame_capture_is_replaying (cap));

	lrg_frame_capture_set_scrub (cap, -5);
	g_assert_cmpint (lrg_frame_capture_get_scrub (cap), ==, -1);

	lrg_frame_capture_set_named_texture (cap, "depth", 7, 32, 16);
	lrg_frame_capture_set_named_texture (cap, "depth", 9, 64, 32);
	g_assert_cmpuint (lrg_frame_capture_get_named_texture (cap, "depth", &w, &h), ==, 9);
	g_assert_cmpint (w, ==, 64);
	g_assert_cmpint (h, ==, 32);
	lrg_frame_capture_set_named_texture (cap, "depth", 0, 0, 0);
	g_assert_cmpuint (lrg_frame_capture_get_named_texture (cap, "depth", NULL, NULL), ==, 0);

	g_assert_false (lrg_frame_capture_replay (cap, &error));
	g_assert_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU);
	g_clear_error (&error);
	g_assert_false (lrg_frame_capture_save_texture (cap, 1, 4, 4, "/tmp/lrg-no-window.png", &error));
	g_assert_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU);
}

static void
test_rframe_bytes (void)
{
	g_autoptr(LrgFrameCapture) cap = lrg_frame_capture_new ();
	g_autoptr(LrgFrameCapture) loaded = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *dir = g_dir_make_tmp ("lrg-rframe-XXXXXX", NULL);
	g_autofree gchar *path = NULL;
	g_autofree gchar *a = NULL;
	g_autofree gchar *b = NULL;
	g_autofree gchar *file_bytes = NULL;
	g_autofree gchar *again = NULL;
	LrgFrameCall      call;
	gsize             la = 0;
	gsize             lb = 0;
	gsize             lf = 0;
	gsize             lg = 0;

	fill_call (&call, "opaque", LRG_FRAME_CALL_KIND_PRIMITIVE, "cube", 12);
	lrg_frame_capture_arm (cap);
	lrg_frame_capture_begin_frame (cap, 64, 64);
	lrg_frame_capture_note (cap, &call);
	lrg_frame_capture_end_frame (cap);

	a = lrg_frame_capture_save_to_data (cap, &la);
	b = lrg_frame_capture_save_to_data (cap, &lb);
	g_assert_cmpuint (la, >, 0);
	g_assert_cmpuint (la, ==, lb);
	g_assert_cmpint (memcmp (a, b, la), ==, 0);
	g_assert_nonnull (strstr (a, "format: rframe\n"));

	path = g_build_filename (dir, "frame.rframe", NULL);
	g_assert_true (lrg_frame_capture_save_to_file (cap, path, &error));
	g_assert_no_error (error);
	loaded = lrg_frame_capture_new_from_file (path, &error);
	g_assert_no_error (error);
	g_assert_nonnull (loaded);
	g_assert_true (lrg_frame_capture_is_captured (loaded));
	g_assert_cmpuint (lrg_frame_capture_get_n_calls (loaded), ==, 1);
	again = lrg_frame_capture_save_to_data (loaded, &lg);
	g_file_get_contents (path, &file_bytes, &lf, &error);
	g_assert_no_error (error);
	g_assert_cmpuint (lf, ==, lg);
	g_assert_cmpint (memcmp (file_bytes, again, lf), ==, 0);

	g_assert_null (lrg_frame_capture_new_from_data ("format: rframe\nversion: 99\n", -1, &error));
	g_assert_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_INVALID);

	g_unlink (path);
	g_rmdir (dir);
}

static void
test_renderer_headless (void)
{
	g_autoptr(LrgWorld3DRenderer) renderer = lrg_world3d_renderer_new ();
	g_autoptr(LrgFrameCapture) cap = lrg_frame_capture_new ();
	g_autoptr(LrgRenderList) list = lrg_render_list_new ();
	g_autoptr(LrgProfiler) borrowed = NULL;
	g_autoptr(LrgProfilerCapture) prof_cap = NULL;
	LrgRenderItem  item;
	LrgWorld3DView view;
	guint          i;
	guint          calls = 0;
	gboolean       saw_opaque = FALSE;

	lrg_world3d_renderer_set_headless (renderer, TRUE);
	lrg_world3d_renderer_set_flags (renderer, LRG_WORLD3D_RENDER_FLAG_SKY);
	lrg_world3d_renderer_set_frame_capture (renderer, cap);
	g_assert_true (lrg_world3d_renderer_get_frame_capture (renderer) == cap);

	lrg_render_item_init (&item);
	lrg_render_list_add (list, &item);
	lrg_render_list_sort (list);
	lrg_world3d_view_init (&view, 128.0f, 96.0f);

	borrowed = lrg_profiler_get_default ();
	g_object_ref (borrowed);
	lrg_profiler_set_enabled (borrowed, TRUE);
	lrg_profiler_begin_frame (borrowed);

	lrg_frame_capture_arm (cap);
	g_assert_false (lrg_world3d_renderer_render_list (renderer, list, &view));
	g_assert_cmpuint (lrg_frame_capture_get_n_calls (cap), >, 0);
	for (i = 0; i < lrg_frame_capture_get_n_calls (cap); i++)
	{
		const LrgFrameCall *call = lrg_frame_capture_get_call (cap, i);

		if (g_strcmp0 (call->pass, "opaque") == 0 && call->kind == LRG_FRAME_CALL_KIND_PRIMITIVE)
			saw_opaque = TRUE;
	}
	g_assert_true (saw_opaque);
	g_assert_cmpuint (lrg_frame_capture_count_pass (cap, "sky"), >=, 1);

	lrg_profiler_end_frame (borrowed);
	prof_cap = lrg_profiler_capture (borrowed, 1);
	g_assert_cmpint (lrg_profiler_capture_get_section_total_us (prof_cap, 0, "world3d.opaque", &calls), >=, 0);
	g_assert_cmpuint (calls, >=, 1);
	lrg_profiler_set_enabled (borrowed, FALSE);
	lrg_profiler_clear (borrowed);

	/* Replay still needs a window; headless recording does not. */
	{
		g_autoptr(GError) error = NULL;

		g_assert_false (lrg_frame_capture_replay (cap, &error));
		g_assert_error (error, LRG_FRAME_CAPTURE_ERROR, LRG_FRAME_CAPTURE_ERROR_GPU);
	}
}

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	if (display == NULL || display[0] == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-frame-capture");
	return IsWindowReady ();
}

typedef struct
{
	guint  n;
	gint64 duration_us;
} GpuGot;

static void
on_gpu (guint64      frame_index,
        const gchar *name,
        guint        depth,
        gint64       start_us,
        gint64       duration_us,
        gpointer     user_data)
{
	GpuGot *got = (GpuGot *) user_data;

	(void) frame_index;
	(void) name;
	(void) depth;
	(void) start_us;
	got->n++;
	got->duration_us = duration_us;
}

static void
test_replay_window (void)
{
	g_autoptr(LrgWorld3DRenderer) renderer = NULL;
	g_autoptr(LrgFrameCapture) cap = NULL;
	g_autoptr(LrgRenderList) list = NULL;
	g_autoptr(LrgGpuTimer) timer = NULL;
	g_autoptr(GError) error = NULL;
	LrgRenderItem  item;
	LrgWorld3DView view;
	GpuGot         got;
	gint           w = 0;
	gint           h = 0;
	guint          i;

	if (!gpu_init ())
	{
		g_test_skip ("No X11 display (run under build-aux/xrun.sh)");
		return;
	}

	renderer = lrg_world3d_renderer_new ();
	cap = lrg_frame_capture_new ();
	list = lrg_render_list_new ();
	lrg_world3d_renderer_set_flags (renderer, LRG_WORLD3D_RENDER_FLAG_SKY | LRG_WORLD3D_RENDER_FLAG_CLEAR);
	lrg_world3d_renderer_set_frame_capture (renderer, cap);
	lrg_render_item_init (&item);
	lrg_render_list_add (list, &item);
	lrg_render_list_sort (list);
	lrg_world3d_view_init (&view, 64.0f, 64.0f);

	lrg_frame_capture_arm (cap);
	lrg_world3d_renderer_render_list (renderer, list, &view);
	g_assert_true (lrg_frame_capture_is_captured (cap));
	g_assert_cmpuint (lrg_frame_capture_get_n_calls (cap), >, 0);

	lrg_frame_capture_set_scrub (cap, 0);
	g_assert_true (lrg_frame_capture_replay (cap, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_frame_capture_get_replay_texture (cap, &w, &h), >, 0);
	g_assert_cmpint (w, ==, 64);
	g_assert_cmpint (h, ==, 64);

	timer = lrg_gpu_timer_new ();
	if (lrg_gpu_timer_is_supported (timer))
	{
		memset (&got, 0, sizeof got);
		for (i = 0; i < 6; i++)
		{
			lrg_gpu_timer_begin_frame (timer, i);
			lrg_gpu_timer_begin (timer, "window.section");
			lrg_gpu_timer_end (timer);
			lrg_gpu_timer_end_frame (timer);
			lrg_gpu_timer_collect (timer, on_gpu, &got);
		}
		g_test_message ("gpu timer collected %u sections, last %lld us (in-flight results may still be 0)",
		                got.n, (long long) got.duration_us);
	}
	else
		g_test_message ("gpu timer queries are not supported on this GL context");
	lrg_gpu_timer_release (timer);

	CloseWindow ();
}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add_func ("/frame-capture/record", test_record_and_scrub);
	g_test_add_func ("/frame-capture/rframe", test_rframe_bytes);
	g_test_add_func ("/frame-capture/renderer-headless", test_renderer_headless);
	g_test_add_func ("/frame-capture/replay-window", test_replay_window);
	return g_test_run ();
}
