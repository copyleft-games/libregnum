/* test-editor-app-memnet.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Memory and network panels: actions, IPC and a one-second history
 * sample driven by synthetic editor frames. The shell docks both
 * panels; this test docks them only when a build has not.
 */

#include "editor-app-fixture.h"

static void
assert_ipc_error (EditorAppFixture *f,
                  const gchar      *request,
                  const gchar      *code)
{
	g_autofree gchar *reply = efx_ipc (f, request);

	g_assert_nonnull (strstr (reply, code));
}

/* efx_frames() drains the default main context until it is idle.
 * tiles_shortcut_idle() stays ready (G_SOURCE_CONTINUE), so that drain
 * never returns. process_frame() itself does not iterate the context,
 * and the panel taps run from the canvas step inside it. */
static void
pump (EditorAppFixture *f,
      gint              n)
{
	gint i;

	for (i = 0; i < n; i++)
	{
		LrgCanvasInput input;

		lrg_canvas_input_init (&input);
		input.delta_time = EFX_DT;
		lrg_editor_app_process_frame (f->app, &input, EFX_W, EFX_H);
	}
}

static void
dock_one (EditorAppFixture *f,
          const gchar      *id,
          const gchar      *title,
          LrgWidget        *content)
{
	LrgDockPanel *dock = lrg_dock_panel_new (id, title);

	lrg_dock_panel_set_child (dock, content);
	g_object_unref (content);
	lrg_editor_app_add_panel (f->app, dock, "tab:assets");
	g_object_unref (dock);
}

static void
test_memnet (EditorAppFixture *f,
             gconstpointer     data)
{
	LrgEditorActions       *actions;
	LrgEditorMemoryPanel   *memory;
	LrgEditorMemoryPanel   *memory_again;
	LrgEditorNetworkPanel  *network;
	LrgDockArea            *dock;
	LrgNetStats            *stats;
	g_autoptr(JsonNode)     status = NULL;
	g_autoptr(JsonNode)     snap = NULL;
	g_autoptr(JsonNode)     diff = NULL;
	g_autoptr(JsonNode)     cleared = NULL;
	g_autoptr(JsonNode)     history = NULL;
	g_autoptr(JsonNode)     shown = NULL;
	g_autoptr(JsonNode)     net = NULL;
	g_autoptr(JsonNode)     net_hist = NULL;
	g_autofree gchar       *id = NULL;
	g_autofree gchar       *diff_req = NULL;
	JsonObject             *object;
	JsonArray              *samples;

	memory = lrg_editor_memory_panel_new (f->app);
	dock = lrg_editor_app_get_dock (f->app);
	if (lrg_dock_area_get_panel (dock, LRG_EDITOR_MEMORY_PANEL_ID) == NULL)
		dock_one (f, LRG_EDITOR_MEMORY_PANEL_ID, "Memory", LRG_WIDGET (memory));
	else
		g_object_unref (memory);
	memory = lrg_editor_memory_panel_find (f->app);
	g_assert_nonnull (memory);
	memory_again = lrg_editor_memory_panel_new (f->app);
	g_assert_true (memory_again == memory);
	g_object_unref (memory_again);

	network = lrg_editor_network_panel_new (f->app);
	if (lrg_dock_area_get_panel (dock, LRG_EDITOR_NETWORK_PANEL_ID) == NULL)
		dock_one (f, LRG_EDITOR_NETWORK_PANEL_ID, "Network", LRG_WIDGET (network));
	else
		g_object_unref (network);
	network = lrg_editor_network_panel_find (f->app);
	g_assert_nonnull (network);
	stats = lrg_editor_network_panel_get_stats (network);
	g_assert_true (stats == lrg_net_stats_get_default ());

	actions = lrg_editor_app_get_actions (f->app);
	g_assert_true (lrg_editor_actions_has (actions, "memory.show-panel"));
	g_assert_true (lrg_editor_actions_has (actions, "memory.snapshot"));
	g_assert_true (lrg_editor_actions_has (actions, "network.show-panel"));
	g_assert_true (lrg_editor_actions_has (actions, "network.toggle"));
	g_assert_true (lrg_editor_actions_has (actions, "network.reset"));

	pump (f, 3);
	g_assert_nonnull (efx_find (f, "memory-toolbar"));
	g_assert_nonnull (efx_find (f, "memory-graph"));
	g_assert_nonnull (efx_find (f, "memory-types"));
	g_assert_nonnull (efx_find (f, "memory-resources"));
	g_assert_nonnull (efx_find (f, "network-toolbar"));
	g_assert_nonnull (efx_find (f, "network-graph"));
	g_assert_nonnull (efx_find (f, "network-peers"));
	g_assert_nonnull (efx_find (f, "network-messages"));

	status = efx_ipc_result (f, "{'id':1,'method':'memory.status'}");
	g_assert_true (json_object_has_member (json_node_get_object (status), "instance_counts"));
	g_assert_true (json_object_has_member (json_node_get_object (status), "asset_manager"));
	{
		g_autoptr(LrgAssetManager) manager = lrg_asset_manager_new ();

		lrg_memory_profiler_set_asset_manager (lrg_editor_memory_panel_get_profiler (memory), manager);
	}
	g_clear_pointer (&status, json_node_unref);
	status = efx_ipc_result (f, "{'id':1,'method':'memory.status'}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (status), "asset_manager"));

	shown = efx_ipc_result (f, "{'id':1,'method':'memory.show'}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (shown), "shown"));
	g_clear_pointer (&shown, json_node_unref);

	lrg_dock_area_present_panel (dock, lrg_dock_area_get_panel (dock, LRG_EDITOR_MEMORY_PANEL_ID));
	pump (f, 70);
	history = efx_ipc_result (f, "{'id':1,'method':'memory.history'}");
	object = json_node_get_object (history);
	g_assert_cmpint (json_object_get_int_member (object, "interval"), ==, 1);
	samples = json_object_get_array_member (object, "instances");
	g_assert_cmpuint (json_array_get_length (samples), >=, 1);

	snap = efx_ipc_result (f, "{'id':1,'method':'memory.snapshot','params':{'label':'t'}}");
	id = g_strdup (json_object_get_string_member (json_node_get_object (snap), "id"));
	g_assert_true (g_str_has_prefix (id, "m"));
	diff_req = g_strdup_printf ("{'id':1,'method':'memory.diff','params':{'from':'%s'}}", id);
	diff = efx_ipc_result (f, diff_req);
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (diff), "to"), ==, "live");
	cleared = efx_ipc_result (f, "{'id':1,'method':'memory.clear_snapshots'}");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (cleared), "removed"), >=, 1);

	assert_ipc_error (f, "{'id':1,'method':'memory.diff','params':{'from':'no-such'}}", "not-found");
	assert_ipc_error (f, "{'id':1,'method':'memory.diff','params':{}}", "invalid-params");
	assert_ipc_error (f, "{'id':1,'method':'memory.types','params':{'sort':'nope'}}", "invalid-params");
	assert_ipc_error (f, "{'id':1,'method':'memory.history','params':{'samples':-1}}", "invalid-params");

	g_clear_pointer (&shown, json_node_unref);
	shown = efx_ipc_result (f, "{'id':1,'method':'network.reset'}");
	g_clear_pointer (&shown, json_node_unref);
	shown = efx_ipc_result (f, "{'id':1,'method':'network.enable','params':{'enabled':true}}");
	g_clear_pointer (&shown, json_node_unref);
	lrg_net_stats_record_received (stats, "127.0.0.1:9", "data", 16);
	net = efx_ipc_result (f, "{'id':1,'method':'network.stats','params':{'window':10}}");
	object = json_object_get_object_member (json_node_get_object (net), "totals");
	g_assert_cmpint (json_object_get_int_member (object, "packets_in"), >=, 1);
	g_assert_true (json_object_get_boolean_member (json_node_get_object (net), "enabled"));

	net_hist = efx_ipc_result (f, "{'id':1,'method':'network.history','params':{'metric':'bytes_in','seconds':5}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (net_hist), "metric"), ==, "bytes-in");
	g_assert_cmpint (json_object_get_int_member (json_node_get_object (net_hist), "interval"), ==, 1);

	assert_ipc_error (f, "{'id':1,'method':'network.stats','params':{'peer':'no-such:1'}}", "not-found");
	assert_ipc_error (f, "{'id':1,'method':'network.history','params':{}}", "invalid-params");
	assert_ipc_error (f, "{'id':1,'method':'network.history','params':{'peer':'no-such:1','metric':'lost'}}", "not-found");
	assert_ipc_error (f, "{'id':1,'method':'network.enable','params':{}}", "invalid-params");
	assert_ipc_error (f, "{'id':1,'method':'network.stats','params':{'window':0}}", "invalid-params");

	shown = efx_ipc_result (f, "{'id':1,'method':'network.show'}");
	g_assert_true (json_object_get_boolean_member (json_node_get_object (shown), "shown"));
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);
	g_test_add ("/editor-app/memnet/panels", EditorAppFixture, NULL, efx_setup, test_memnet, efx_teardown);
	return g_test_run ();
}
