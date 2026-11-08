/* test-editor-app-frame.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Profiler panel and frame debugger over the headless editor canvas:
 * profiler.* and frame.* IPC, the flame view, and a recorded call list
 * with no GL replay.
 */

#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <libregnum.h>

#include "editor-app-fixture.h"

/* A frame without draining the main context. process_frame() steps the
 * canvas, which is all the profiler tap needs. */
static void
frame_once (EditorAppFixture *f)
{
	LrgCanvasInput input;

	lrg_canvas_input_init (&input);
	input.pointer_x = f->x;
	input.pointer_y = f->y;
	input.buttons_down = f->buttons;
	input.buttons_pressed = f->buttons & ~f->prev_buttons;
	input.buttons_released = f->prev_buttons & ~f->buttons;
	input.modifiers = f->mods;
	input.delta_time = EFX_DT;
	lrg_editor_app_process_frame (f->app, &input, EFX_W, EFX_H);
	f->prev_buttons = f->buttons;
}

static void
run_frames (EditorAppFixture *f,
        gint              n)
{
	gint i;

	for (i = 0; i < n; i++)
		frame_once (f);
}

static LrgWidget *
present (EditorAppFixture *f,
         const gchar      *id)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *panel = lrg_dock_area_get_panel (dock, id);

	g_assert_nonnull (panel);
	lrg_dock_area_present_panel (dock, panel);
	run_frames (f, 2);
	return lrg_dock_panel_get_child (panel);
}

static gchar *
reply_has (EditorAppFixture *f,
           const gchar      *request,
           const gchar      *needle)
{
	gchar *reply = efx_ipc (f, request);

	if (reply == NULL || strstr (reply, needle) == NULL)
		g_printerr ("IPC reply [%s] missing [%s]\n", reply != NULL ? reply : "(null)", needle);
	g_assert_nonnull (reply);
	g_assert_nonnull (strstr (reply, needle));
	return reply;
}

static void
test_profiler_and_frame (EditorAppFixture *f,
                         gconstpointer     data)
{
	g_autoptr(JsonNode) set_ok = NULL;
	g_autoptr(JsonNode) sample = NULL;
	g_autoptr(JsonNode) captured = NULL;
	g_autoptr(JsonNode) frames = NULL;
	g_autoptr(JsonNode) one = NULL;
	g_autoptr(JsonNode) cmp = NULL;
	g_autoptr(JsonNode) view = NULL;
	g_autoptr(JsonNode) saved = NULL;
	g_autoptr(JsonNode) loaded = NULL;
	g_autoptr(JsonNode) shown = NULL;
	g_autoptr(JsonNode) armed = NULL;
	g_autoptr(JsonNode) status = NULL;
	g_autoptr(JsonNode) calls = NULL;
	g_autoptr(JsonNode) call = NULL;
	g_autoptr(JsonNode) scrub = NULL;
	g_autoptr(JsonNode) fsave = NULL;
	g_autoptr(JsonNode) fload = NULL;
	g_autoptr(JsonNode) empty_pass = NULL;
	g_autofree gchar *save_req = NULL;
	g_autofree gchar *load_req = NULL;
	g_autofree gchar *cmp_req = NULL;
	g_autofree gchar *frame_path = NULL;
	g_autofree gchar *prof_path = NULL;
	g_autofree gchar *tex_req = NULL;
	g_autofree gchar *miss_req = NULL;
	gchar            *err = NULL;
	JsonObject       *o;
	JsonArray        *arr;
	JsonArray        *sections;
	LrgEditorProfilerPanel *panel;
	LrgEditorFrameDebugger *debugger;
	LrgFrameCapture        *cap;
	LrgFrameCall            draw;
	gint64                  a;
	gint64                  b;
	guint                   i;

	(void) data;
	prof_path = g_build_filename (f->dir, "cap.rprof", NULL);
	frame_path = g_build_filename (f->dir, "frame.rframe", NULL);

	save_req = g_strdup_printf ("{'id':1,'method':'profiler.save','params':{'path':'%s'}}", prof_path);
	err = reply_has (f, save_req, "not-found");
	g_free (err);

	err = reply_has (f, "{'id':2,'method':'profiler.set','params':{'ring':7}}", "invalid-params");
	g_free (err);

	set_ok = efx_ipc_result (f, "{'id':3,'method':'profiler.set','params':{'enable':true,'gpu':false,'ring':32}}");
	o = json_node_get_object (set_ok);
	g_assert_true (json_object_get_boolean_member (o, "enabled"));
	g_assert_true (json_object_get_boolean_member (o, "pinned"));
	g_assert_false (json_object_get_boolean_member (o, "gpu"));
	g_assert_false (json_object_get_boolean_member (o, "gpu_supported"));
	g_assert_cmpint (json_object_get_int_member (o, "ring"), ==, 32);

	run_frames (f, 4);

	sample = efx_ipc_result (f, "{'id':4,'method':'profiler.sample','params':{'frames':8}}");
	o = json_node_get_object (sample);
	g_assert_true (json_object_has_member (o, "gpu"));
	g_assert_true (json_object_has_member (o, "sections"));
	g_assert_true (json_object_has_member (o, "frames"));

	captured = efx_ipc_result (f, "{'id':5,'method':'profiler.capture','params':{'frames':120}}");
	o = json_node_get_object (captured);
	g_assert_cmpint (json_object_get_int_member (o, "frames"), >=, 2);
	g_assert_nonnull (json_object_get_array_member (o, "threads"));

	frames = efx_ipc_result (f, "{'id':6,'method':'profiler.frames','params':{'count':60}}");
	g_assert_cmpint (json_node_get_node_type (frames), ==, JSON_NODE_ARRAY);
	arr = json_node_get_array (frames);
	g_assert_cmpint (json_array_get_length (arr), >=, 2);
	a = json_object_get_int_member (json_array_get_object_element (arr, 0), "index");
	b = json_object_get_int_member (json_array_get_object_element (arr, json_array_get_length (arr) - 1), "index");
	g_assert_true (json_object_has_member (json_array_get_object_element (arr, 0), "duration_ms"));

	one = efx_ipc_result (f, "{'id':7,'method':'profiler.frame'}");
	o = json_node_get_object (one);
	g_assert_cmpint (json_object_get_int_member (o, "index"), ==, b);
	g_assert_nonnull (json_object_get_array_member (o, "events"));
	g_assert_nonnull (json_object_get_array_member (o, "gpu"));
	g_assert_nonnull (json_object_get_array_member (o, "counters"));

	cmp_req = g_strdup_printf ("{'id':8,'method':'profiler.compare','params':{'a':%" G_GINT64_FORMAT ",'b':%" G_GINT64_FORMAT "}}", a, b);
	cmp = efx_ipc_result (f, cmp_req);
	o = json_node_get_object (cmp);
	g_assert_cmpint (json_object_get_int_member (json_object_get_object_member (o, "a"), "index"), ==, a);
	g_assert_cmpint (json_object_get_int_member (json_object_get_object_member (o, "b"), "index"), ==, b);
	sections = json_object_get_array_member (o, "sections");
	g_assert_cmpint (json_array_get_length (sections), >=, 1);
	for (i = 1; i < json_array_get_length (sections); i++)
	{
		gdouble prev = json_object_get_double_member (json_array_get_object_element (sections, i - 1), "delta_ms");
		gdouble cur = json_object_get_double_member (json_array_get_object_element (sections, i), "delta_ms");

		if (prev < 0)
			prev = -prev;
		if (cur < 0)
			cur = -cur;
		g_assert_cmpfloat (prev, >=, cur);
	}

	view = efx_ipc_result (f, "{'id':9,'method':'profiler.view','params':{'mode':'flame','show':true}}");
	o = json_node_get_object (view);
	g_assert_cmpstr (json_object_get_string_member (o, "mode"), ==, "flame");
	g_assert_cmpstr (json_object_get_string_member (o, "source"), ==, "capture");
	panel = LRG_EDITOR_PROFILER_PANEL (present (f, "profiler"));
	g_assert_cmpstr (lrg_editor_profiler_panel_get_view_mode (panel), ==, "flame");
	g_assert_true (lrg_editor_profiler_panel_get_capture (panel) != NULL);
	g_assert_nonnull (efx_find (f, "profiler-graph"));
	g_assert_nonnull (efx_find (f, "profiler-lanes"));
	g_assert_nonnull (efx_find (f, "profiler-mode-flame"));

	save_req = g_strdup_printf ("{'id':10,'method':'profiler.save','params':{'path':'%s'}}", prof_path);
	saved = efx_ipc_result (f, save_req);
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (saved), "frames"), >=, 2);
	g_assert_true (g_file_test (prof_path, G_FILE_TEST_EXISTS));

	load_req = g_strdup_printf ("{'id':11,'method':'profiler.load','params':{'path':'%s'}}", prof_path);
	loaded = efx_ipc_result (f, load_req);
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (loaded), "frames"), >=, 2);

	/* The shell docks the profiler's frame debugger. frame.show presents it. */
	g_assert_nonnull (lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "frame"));
	shown = efx_ipc_result (f, "{'id':12,'method':'frame.show'}");
	g_assert_cmpint (json_node_get_node_type (shown), ==, JSON_NODE_OBJECT);
	debugger = LRG_EDITOR_FRAME_DEBUGGER (present (f, "frame"));
	g_assert_nonnull (efx_find (f, "frame-calls"));
	g_assert_nonnull (efx_find (f, "frame-capture"));

	armed = efx_ipc_result (f, "{'id':13,'method':'frame.capture'}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (armed), "armed"));

	cap = lrg_editor_frame_debugger_get_capture (debugger);
	g_assert_true (lrg_frame_capture_is_armed (cap));
	memset (&draw, 0, sizeof draw);
	draw.pass = "clear";
	draw.kind = LRG_FRAME_CALL_KIND_CLEAR;
	draw.shader = "clear";
	draw.instances = 1;
	lrg_frame_capture_begin_frame (cap, 64, 48);
	lrg_frame_capture_note (cap, &draw);
	draw.pass = "opaque";
	draw.kind = LRG_FRAME_CALL_KIND_PRIMITIVE;
	draw.mesh = "cube";
	draw.shader = "lit";
	draw.triangles = 12;
	draw.vertices = 24;
	draw.color[0] = 1.0f;
	draw.color[3] = 1.0f;
	draw.transform[0] = draw.transform[5] = draw.transform[10] = draw.transform[15] = 1.0f;
	lrg_frame_capture_note (cap, &draw);
	lrg_frame_capture_end_frame (cap);

	status = efx_ipc_result (f, "{'id':14,'method':'frame.status'}");
	o = json_node_get_object (status);
	g_assert_true (json_object_get_boolean_member (o, "captured"));
	g_assert_cmpint (json_object_get_int_member (o, "calls"), ==, 2);
	g_assert_cmpint (json_object_get_int_member (o, "width"), ==, 64);
	g_assert_cmpint (json_object_get_int_member (o, "scrub"), ==, -1);

	calls = efx_ipc_result (f, "{'id':15,'method':'frame.calls','params':{'pass':'opaque'}}");
	o = json_node_get_object (calls);
	g_assert_cmpint (json_object_get_int_member (o, "total"), ==, 1);
	g_assert_cmpstr (json_object_get_string_member (json_array_get_object_element (json_object_get_array_member (o, "calls"), 0), "kind"), ==, "primitive");

	empty_pass = efx_ipc_result (f, "{'id':16,'method':'frame.calls','params':{'pass':'no-such-pass'}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (empty_pass), "total"), ==, 0);

	call = efx_ipc_result (f, "{'id':17,'method':'frame.call','params':{'index':1}}");
	o = json_node_get_object (call);
	g_assert_cmpstr (json_object_get_string_member (o, "pass"), ==, "opaque");
	g_assert_cmpstr (json_object_get_string_member (o, "mesh"), ==, "cube");
	g_assert_cmpint (json_object_get_int_member (o, "triangles"), ==, 12);
	g_assert_true (json_object_has_member (o, "shader_id"));
	g_assert_cmpint (json_array_get_length (json_object_get_array_member (o, "transform")), ==, 16);
	g_assert_true (json_object_has_member (json_object_get_object_member (o, "state"), "depth_test"));

	scrub = efx_ipc_result (f, "{'id':18,'method':'frame.scrub','params':{'index':0}}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (scrub), "scrub"), ==, 0);
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (scrub), "calls"), ==, 2);
	g_assert_cmpint (lrg_editor_frame_debugger_get_scrub (debugger), ==, 0);

	save_req = g_strdup_printf ("{'id':19,'method':'frame.save','params':{'path':'%s'}}", frame_path);
	fsave = efx_ipc_result (f, save_req);
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (fsave), "calls"), ==, 2);

	load_req = g_strdup_printf ("{'id':20,'method':'frame.load','params':{'path':'%s'}}", frame_path);
	fload = efx_ipc_result (f, load_req);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (fload), "captured"));
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (fload), "calls"), ==, 2);

	tex_req = g_strdup_printf ("{'id':21,'method':'frame.texture','params':{'target':'shadow','path':'%s/shadow.png'}}", f->dir);
	err = reply_has (f, tex_req, "not-found");
	g_free (err);

	/* A missing .rframe is an I/O failure (LRG_FRAME_CAPTURE_ERROR), not
	 * the not-found code used for a missing draw call or texture. */
	miss_req = g_strdup_printf ("{'id':22,'method':'frame.load','params':{'path':'%s/missing.rframe'}}", f->dir);
	err = reply_has (f, miss_req, "No such file");
	g_free (err);

	err = reply_has (f, "{'id':23,'method':'frame.call','params':{'index':99}}", "not-found");
	g_free (err);

}

int
main (int argc, char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	g_test_add ("/editor-app/frame/profiler-and-frame", EditorAppFixture, NULL,
	            efx_setup, test_profiler_and_frame, efx_teardown);
	return g_test_run ();
}
