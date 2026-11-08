/* test-editor-app-bake.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The lrgeditor lighting bake (0.9.0 M3): the bake panel (settings as
 * undoable `lighting: settings:` edits, an async bake driven from its
 * chip with phase progress, cancel, auto save of the .rlightmap /
 * .rprobes next to the level with undoable references, clear, the atlas
 * preview and the light-at-cursor readout), the bake.* IPC methods,
 * probe-volume nodes (instantiation into world probe grids, inspector
 * rows, the volume gizmo) and the inspector's lighting rows.  The bake
 * runs on the CPU, so all of it works headless; the GPU case (xrun.sh)
 * renders the authoring viewport with and without the baked lighting.
 */

#include "editor-app-fixture.h"
#include <raylib.h>
#include <math.h>

static gboolean gpu_ready = FALSE;

/* The bake panel, docked by the window as a tab on the inspector. */
static LrgEditorBakePanel *
bake_panel (EditorAppFixture *f)
{
	LrgEditorBakePanel *panel = lrg_editor_bake_panel_find (f->app);
	LrgDockArea        *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel       *p;

	g_assert_nonnull (panel);
	p = lrg_dock_area_get_panel (dock, LRG_EDITOR_BAKE_PANEL_ID);
	g_assert_nonnull (p);
	lrg_dock_area_present_panel (dock, p);
	efx_frames (f, 3);
	return panel;
}

static LrgLevel *
level_of (EditorAppFixture *f)
{
	return lrg_editor_get_level (f->editor);
}

/* A node of @kind under the root at @x, @y, @z. */
static LrgNode *
add_kind (EditorAppFixture  *f,
          const gchar       *name,
          LrgNodeVisualKind  kind,
          gfloat             x,
          gfloat             y,
          gfloat             z)
{
	g_autoptr(LrgNode)       node = lrg_node_new (name);
	g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (kind);

	lrg_node_set_location_xyz (node, x, y, z);
	lrg_node_set_visual (node, visual);
	lrg_editor_add_node (f->editor, node, NULL);
	return node;
}

static void
set_bool (LrgNode     *node,
          const gchar *key,
          gboolean     v)
{
	g_auto(GValue) value = G_VALUE_INIT;

	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, v);
	lrg_node_visual_set_param (lrg_node_get_visual (node), key, &value);
}

static void
set_string (LrgNode     *node,
            const gchar *key,
            const gchar *v)
{
	g_auto(GValue) value = G_VALUE_INIT;

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, v);
	lrg_node_visual_set_param (lrg_node_get_visual (node), key, &value);
}

/*
 * A small lit room: a static floor and box, a baked point light above
 * them and a probe volume around the box; the level saved under @f->dir
 * when @save.
 */
static void
build_scene (EditorAppFixture *f,
             gboolean          save)
{
	LrgNode *floor_node, *box, *light, *volume;

	lrg_editor_new_level (f->editor, "room");
	floor_node = add_kind (f, "Floor", LRG_NODE_VISUAL_PRIMITIVE, 0.0f, -0.1f, 0.0f);
	lrg_node_visual_set_primitive (lrg_node_get_visual (floor_node), LRG_PRIMITIVE_CUBE);
	lrg_node_set_scale_xyz (floor_node, 6.0f, 0.2f, 6.0f);
	set_bool (floor_node, "static_lighting", TRUE);
	box = add_kind (f, "Box", LRG_NODE_VISUAL_PRIMITIVE, 0.5f, 0.5f, 0.0f);
	lrg_node_visual_set_primitive (lrg_node_get_visual (box), LRG_PRIMITIVE_CUBE);
	set_bool (box, "static_lighting", TRUE);
	light = add_kind (f, "Lamp", LRG_NODE_VISUAL_LIGHT, 0.0f, 2.5f, 0.0f);
	set_string (light, "light_type", "point");
	set_string (light, "bake_mode", "baked");
	lrg_node_visual_set_param_double (lrg_node_get_visual (light), "intensity", 2.0);
	lrg_node_visual_set_param_double (lrg_node_get_visual (light), "range", 8.0);
	volume = add_kind (f, "Probes", LRG_NODE_VISUAL_PROBE_VOLUME, 0.0f, 1.5f, 0.0f);
	lrg_node_visual_set_param_double (lrg_node_get_visual (volume), "extents_x", 3.0);
	lrg_node_visual_set_param_double (lrg_node_get_visual (volume), "extents_y", 1.5);
	lrg_node_visual_set_param_double (lrg_node_get_visual (volume), "extents_z", 3.0);
	lrg_node_visual_set_param_double (lrg_node_get_visual (volume), "spacing", 1.5);
	if (save)
	{
		g_autofree gchar *path = g_build_filename (f->dir, "room.rlevel", NULL);

		lrg_editor_set_path (f->editor, path);
	}
}

/* Quick, deterministic settings for tests. */
static void
fast_settings (LrgEditorBakePanel *panel)
{
	g_assert_true (lrg_editor_bake_panel_set_setting (panel, "texels_per_unit", 2.0, NULL));
	g_assert_true (lrg_editor_bake_panel_set_setting (panel, "samples", 8.0, NULL));
	g_assert_true (lrg_editor_bake_panel_set_setting (panel, "bounces", 1.0, NULL));
	g_assert_true (lrg_editor_bake_panel_set_setting (panel, "probe_samples", 16.0, NULL));
	g_assert_true (lrg_editor_bake_panel_set_setting (panel, "max_lightmap_size", 128.0, NULL));
}

/* Runs the main loop (and frames) until the bake stops; 60 s at most. */
static void
wait_bake (EditorAppFixture   *f,
           LrgEditorBakePanel *panel)
{
	gint64 deadline = g_get_monotonic_time () + 60 * G_USEC_PER_SEC;

	while (lrg_editor_bake_panel_get_baking (panel) && g_get_monotonic_time () < deadline)
	{
		g_main_context_iteration (NULL, FALSE);
		efx_frame (f);
		g_usleep (2000);
	}
	g_assert_false (lrg_editor_bake_panel_get_baking (panel));
	efx_frames (f, 2);
}

/* ==========================================================================
 * Settings
 * ========================================================================== */

/* Grid / API edits are undoable `lighting: settings:` keys. */
static void
test_settings (EditorAppFixture *f,
               gconstpointer     data)
{
	LrgEditorBakePanel *panel;
	gdouble             v = 0.0;
	gfloat              tpu = 0.0f;
	guint               samples = 0;
	g_autoptr(GError)   error = NULL;

	build_scene (f, FALSE);
	panel = bake_panel (f);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "bake-scroll")));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "bake-empty")));
	g_assert_false (lrg_level_get_bake_setting (level_of (f), "texels_per_unit", NULL));

	g_assert_true (lrg_editor_bake_panel_set_setting (panel, "texels_per_unit", 4.0, &error));
	g_assert_no_error (error);
	g_assert_true (lrg_level_get_bake_setting (level_of (f), "texels_per_unit", &v));
	g_assert_cmpfloat_with_epsilon (v, 4.0, 1e-6);
	g_object_get (lrg_editor_bake_panel_get_settings (panel), "texels-per-unit", &tpu, NULL);
	g_assert_cmpfloat_with_epsilon (tpu, 4.0f, 1e-6f);

	/* one undo step; the settings object follows */
	lrg_editor_undo (f->editor);
	g_assert_false (lrg_level_get_bake_setting (level_of (f), "texels_per_unit", NULL));
	g_object_get (lrg_editor_bake_panel_get_settings (panel), "texels-per-unit", &tpu, NULL);
	g_assert_cmpfloat_with_epsilon (tpu, 8.0f, 1e-6f);
	lrg_editor_redo (f->editor);
	g_assert_true (lrg_level_get_bake_setting (level_of (f), "texels_per_unit", &v));
	g_assert_cmpfloat_with_epsilon (v, 4.0, 1e-6);

	/* a property set on the object (what the grid does) is an edit too;
	 * '-' names work as well, out-of-range values clamp */
	g_object_set (lrg_editor_bake_panel_get_settings (panel), "samples", 32u, NULL);
	g_assert_true (lrg_level_get_bake_setting (level_of (f), "samples", &v));
	g_assert_cmpfloat_with_epsilon (v, 32.0, 1e-6);
	g_assert_true (lrg_editor_bake_panel_set_setting (panel, "bounces", 99.0, NULL));
	g_assert_true (lrg_level_get_bake_setting (level_of (f), "bounces", &v));
	g_assert_cmpfloat_with_epsilon (v, 16.0, 1e-6);
	g_assert_true (lrg_editor_bake_panel_set_setting (panel, "denoise", 0.0, NULL));
	g_assert_true (lrg_level_get_bake_setting (level_of (f), "denoise", &v));
	g_assert_cmpfloat_with_epsilon (v, 0.0, 1e-6);
	g_assert_false (lrg_editor_bake_panel_set_setting (panel, "no_such_key", 1.0, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);

	/* the grid shows the settings */
	g_assert_nonnull (lrg_property_source_find_property (
		lrg_property_grid_get_source (lrg_editor_bake_panel_get_grid (panel), 0), "texels-per-unit"));

	/* reset drops every key in one step */
	g_assert_true (lrg_editor_bake_panel_reset_settings (panel));
	g_assert_false (lrg_level_has_lighting (level_of (f)));
	g_object_get (lrg_editor_bake_panel_get_settings (panel), "samples", &samples, NULL);
	g_assert_cmpuint (samples, ==, 64);
	lrg_editor_undo (f->editor);
	g_assert_true (lrg_level_get_bake_setting (level_of (f), "samples", &v));
	g_assert_cmpfloat_with_epsilon (v, 32.0, 1e-6);
}

/* ==========================================================================
 * Bake, save, undo, clear
 * ========================================================================== */

static void
test_bake_save (EditorAppFixture *f,
                gconstpointer     data)
{
	LrgEditorBakePanel *panel;
	LrgLightmap3D      *lm;
	g_autofree gchar   *lm_path = g_build_filename (f->dir, "room.rlightmap", NULL);
	g_autofree gchar   *pg_path = g_build_filename (f->dir, "room-0.rprobes", NULL);
	GPtrArray          *refs;
	gfloat              rgb[3];
	const gfloat        on_floor[3] = { -1.5f, 0.001f, 1.5f };
	const gfloat        up[3] = { 0.0f, 1.0f, 0.0f };

	build_scene (f, TRUE);
	panel = bake_panel (f);
	fast_settings (panel);
	g_assert_cmpstr (lrg_editor_bake_panel_get_phase (panel), ==, "idle");
	g_assert_false (lrg_widget_get_enabled (efx_find (f, "bake-save")));

	/* the Bake chip starts it; the chip turns into Cancel meanwhile */
	efx_click_widget (f, efx_find (f, "bake-bake"));
	g_assert_true (lrg_editor_bake_panel_get_baking (panel));
	g_assert_true (lrg_widget_get_visible (efx_find (f, "bake-progress")));
	wait_bake (f, panel);
	g_assert_cmpstr (lrg_editor_bake_panel_get_phase (panel), ==, "done");
	g_assert_cmpfloat (lrg_editor_bake_panel_get_progress (panel), ==, 1.0);
	g_assert_null (lrg_editor_bake_panel_get_error (panel));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "bake-progress")));

	/* two lightmapped objects, one probe grid of the volume's layout */
	lm = lrg_editor_bake_panel_get_lightmap (panel);
	g_assert_nonnull (lm);
	g_assert_cmpuint (lrg_lightmap3d_get_n_entries (lm), ==, 2);
	g_assert_cmpuint (lrg_editor_bake_panel_get_probe_grids (panel)->len, ==, 1);
	{
		LrgProbeGrid *grid = g_ptr_array_index (lrg_editor_bake_panel_get_probe_grids (panel), 0);
		guint         n[3];
		gfloat        mn[3], mx[3];

		lrg_probe_grid_get_resolution (grid, &n[0], &n[1], &n[2]);
		lrg_probe_grid_get_bounds (grid, mn, mx);
		/* 6 / 1.5 + 1 = 5 across, 3 / 1.5 + 1 = 3 up */
		g_assert_cmpuint (n[0], ==, 5);
		g_assert_cmpuint (n[1], ==, 3);
		g_assert_cmpuint (n[2], ==, 5);
		g_assert_cmpfloat_with_epsilon (mn[1], 0.0f, 1e-4f);
		g_assert_cmpfloat_with_epsilon (mx[1], 3.0f, 1e-4f);
		g_assert_cmpuint (lrg_probe_grid_get_n_valid (grid), >, 0);
	}

	/* auto save: files next to the level, references relative to it */
	g_assert_true (g_file_test (lm_path, G_FILE_TEST_IS_REGULAR));
	g_assert_true (g_file_test (pg_path, G_FILE_TEST_IS_REGULAR));
	g_assert_cmpstr (lrg_level_get_lightmap_ref (level_of (f)), ==, "room.rlightmap");
	refs = lrg_level_get_probe_refs (level_of (f));
	g_assert_cmpuint (refs->len, ==, 1);
	g_assert_cmpstr (g_ptr_array_index (refs, 0), ==, "room-0.rprobes");
	g_assert_false (lrg_editor_bake_panel_has_unsaved (panel));
	g_assert_true (lrg_level_get_bake_setting (level_of (f), "samples", NULL));   /* settings kept */

	/* the viewport now reads the saved files */
	g_assert_nonnull (lrg_editor_scene_renderer_get_baked_lightmap (lrg_editor_app_get_scene_renderer (f->app)));
	g_assert_cmpuint (lrg_lightmap3d_get_n_entries (
		lrg_editor_scene_renderer_get_baked_lightmap (lrg_editor_app_get_scene_renderer (f->app))), ==, 2);
	g_assert_cmpuint (lrg_editor_scene_renderer_get_baked_probe_grids (
		lrg_editor_app_get_scene_renderer (f->app))->len, ==, 1);

	/* the floor under the lamp is lit by the baked light */
	g_assert_true (lrg_editor_bake_panel_evaluate (panel, on_floor, up, rgb));
	g_assert_cmpfloat (rgb[0] + rgb[1] + rgb[2], >, 0.01f);

	/* the atlas preview shows the page */
	g_assert_true (lrg_widget_get_visible (efx_find (f, "bake-atlas-row")));
	lrg_editor_bake_panel_set_page (panel, 7);
	g_assert_cmpuint (lrg_editor_bake_panel_get_page (panel), ==, lrg_lightmap3d_get_n_pages (lm) - 1);

	/* undo takes the references away (one step), redo puts them back */
	lrg_editor_undo (f->editor);
	g_assert_null (lrg_level_get_lightmap_ref (level_of (f)));
	g_assert_cmpuint (lrg_level_get_probe_refs (level_of (f))->len, ==, 0);
	g_assert_null (lrg_editor_scene_renderer_get_baked_lightmap (lrg_editor_app_get_scene_renderer (f->app)));
	lrg_editor_redo (f->editor);
	g_assert_cmpstr (lrg_level_get_lightmap_ref (level_of (f)), ==, "room.rlightmap");

	/* the level round trips its lighting block */
	{
		g_autoptr(LrgLevelSerializer) ser = lrg_level_serializer_new ();
		g_autofree gchar             *yaml = lrg_level_serializer_save_to_data (ser, level_of (f), NULL);

		g_assert_nonnull (yaml);
		g_assert_nonnull (strstr (yaml, "room.rlightmap"));
		g_assert_nonnull (strstr (yaml, "probe-volume"));
	}

	/* Clear: one undo step, files stay */
	efx_frames (f, 2);
	efx_click_widget (f, efx_find (f, "bake-clear"));
	g_assert_null (lrg_level_get_lightmap_ref (level_of (f)));
	g_assert_true (g_file_test (lm_path, G_FILE_TEST_IS_REGULAR));
	lrg_editor_undo (f->editor);
	g_assert_cmpstr (lrg_level_get_lightmap_ref (level_of (f)), ==, "room.rlightmap");
}

/* An unsaved level keeps the bake in memory until Save. */
static void
test_unsaved (EditorAppFixture *f,
              gconstpointer     data)
{
	LrgEditorBakePanel *panel;
	g_autoptr(GError)   error = NULL;
	g_autofree gchar   *out = g_build_filename (f->dir, "bakes", NULL);
	g_autofree gchar   *lm_path = g_build_filename (out, "level.rlightmap", NULL);

	build_scene (f, FALSE);
	panel = bake_panel (f);
	fast_settings (panel);
	g_assert_true (lrg_editor_bake_panel_bake (panel, &error));
	g_assert_no_error (error);
	wait_bake (f, panel);
	g_assert_true (lrg_editor_bake_panel_has_unsaved (panel));
	g_assert_null (lrg_level_get_lightmap_ref (level_of (f)));
	g_assert_true (lrg_widget_get_enabled (efx_find (f, "bake-save")));
	/* shown in the viewport before it is saved */
	g_assert_true (lrg_editor_scene_renderer_get_baked_lightmap (lrg_editor_app_get_scene_renderer (f->app)) ==
	               lrg_editor_bake_panel_get_lightmap (panel));

	/* no level file: Save needs a directory */
	g_assert_false (lrg_editor_bake_panel_save (panel, NULL, &error));
	g_assert_nonnull (error);
	g_clear_error (&error);
	g_assert_true (lrg_editor_bake_panel_save (panel, out, &error));
	g_assert_no_error (error);
	g_assert_true (g_file_test (lm_path, G_FILE_TEST_IS_REGULAR));
	g_assert_cmpstr (lrg_level_get_lightmap_ref (level_of (f)), ==, lm_path);
	g_assert_false (lrg_editor_bake_panel_has_unsaved (panel));
}

/* Cancel keeps the previous lighting; nothing static fails early. */
static void
test_cancel_and_errors (EditorAppFixture *f,
                        gconstpointer     data)
{
	LrgEditorBakePanel *panel;
	g_autoptr(GError)   error = NULL;

	lrg_editor_new_level (f->editor, "empty");
	panel = bake_panel (f);
	g_assert_false (lrg_editor_bake_panel_bake (panel, &error));
	g_assert_nonnull (strstr (error->message, "static_lighting"));
	g_clear_error (&error);

	build_scene (f, TRUE);
	panel = bake_panel (f);
	g_assert_true (lrg_editor_bake_panel_set_setting (panel, "samples", 4096.0, NULL));
	g_assert_true (lrg_editor_bake_panel_set_setting (panel, "texels_per_unit", 16.0, NULL));
	g_assert_true (lrg_editor_bake_panel_bake (panel, &error));
	g_assert_false (lrg_editor_bake_panel_bake (panel, &error));   /* one at a time */
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
	g_clear_error (&error);
	g_assert_true (lrg_editor_bake_panel_cancel (panel));
	wait_bake (f, panel);
	g_assert_cmpstr (lrg_editor_bake_panel_get_phase (panel), ==, "cancelled");
	g_assert_null (lrg_editor_bake_panel_get_lightmap (panel));
	g_assert_null (lrg_level_get_lightmap_ref (level_of (f)));
	g_assert_false (lrg_editor_bake_panel_cancel (panel));
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	LrgEditorBakePanel *panel;
	JsonObject         *o;
	gint64              deadline;

	build_scene (f, TRUE);
	panel = bake_panel (f);

	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'bake.set','params':{'values':"
		                                           "{'texels_per_unit':2,'samples':8,'bounces':1,'probe_samples':16,"
		                                           "'denoise':true}}}");

		o = json_node_get_object (r);
		g_assert_cmpfloat_with_epsilon (json_object_get_double_member (json_object_get_object_member (o, "settings"),
		                                                               "texels_per_unit"), 2.0, 1e-6);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "set_keys")), ==, 5);
	}
	/* the batch is one undo step */
	lrg_editor_undo (f->editor);
	g_assert_false (lrg_level_has_lighting (level_of (f)));
	lrg_editor_redo (f->editor);
	g_assert_true (lrg_level_get_bake_setting (level_of (f), "samples", NULL));

	{
		g_autofree gchar *reply = efx_ipc (f, "{'id':2,'method':'bake.set','params':{'key':'nope','value':1}}");

		g_assert_nonnull (strstr (reply, "error"));
	}

	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':3,'method':'bake.start'}");

		g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "started"));
		g_assert_cmpint (json_object_get_int_member (json_node_get_object (r), "objects"), ==, 2);
	}
	deadline = g_get_monotonic_time () + 60 * G_USEC_PER_SEC;
	for (;;)
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':4,'method':'bake.progress'}");

		o = json_node_get_object (r);
		if (!json_object_get_boolean_member (o, "baking") || g_get_monotonic_time () > deadline)
		{
			g_assert_cmpstr (json_object_get_string_member (o, "phase"), ==, "done");
			break;
		}
		efx_frame (f);
		g_usleep (2000);
	}

	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':5,'method':'bake.get'}");
		JsonObject         *res;

		o = json_node_get_object (r);
		g_assert_cmpstr (json_object_get_string_member (o, "lightmap"), ==, "room.rlightmap");
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "probes")), ==, 1);
		res = json_object_get_object_member (o, "result");
		g_assert_cmpint (json_object_get_int_member (res, "entries"), ==, 2);
		g_assert_cmpint (json_object_get_int_member (res, "grids"), ==, 1);
		g_assert_cmpint (json_object_get_int_member (res, "probes"), ==, 75);
		g_assert_false (json_object_get_boolean_member (res, "unsaved"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':6,'method':'bake.evaluate','params':"
		                                           "{'position':[-1.5,0.001,1.5],'normal':[0,1,0]}}");
		JsonArray          *rgb = json_object_get_array_member (json_node_get_object (r), "rgb");

		g_assert_cmpuint (json_array_get_length (rgb), ==, 3);
		g_assert_cmpfloat (json_object_get_double_member (json_node_get_object (r), "luminance"), >, 0.0);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':7,'method':'bake.preview','params':{'show':false}}");

		g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "show"));
		g_assert_false (lrg_editor_scene_renderer_get_show_baked (lrg_editor_app_get_scene_renderer (f->app)));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':8,'method':'bake.preview','params':{'show':true}}");

		g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "show"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':9,'method':'bake.clear'}");

		g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "cleared"));
		g_assert_null (lrg_level_get_lightmap_ref (level_of (f)));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':10,'method':'bake.show'}");

		g_assert_true (json_object_get_boolean_member (json_node_get_object (r), "shown"));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':11,'method':'bake.cancel'}");

		g_assert_false (json_object_get_boolean_member (json_node_get_object (r), "cancelled"));
	}
	g_assert_nonnull (panel);
}

/* ==========================================================================
 * Probe volumes and the inspector
 * ========================================================================== */

/* Probe-volume nodes become unbaked world grids; baked refs replace them. */
static void
test_probe_volume_instantiate (EditorAppFixture *f,
                               gconstpointer     data)
{
	g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
	g_autoptr(LrgWorld3D) world2 = lrg_world3d_new ();
	g_autoptr(GError)     error = NULL;
	LrgProbeGrid         *grid;
	gfloat                mn[3], mx[3];
	guint                 n[3];

	build_scene (f, FALSE);
	lrg_level_set_bake_setting (level_of (f), "probe_spacing", 1.0);
	g_assert_true (lrg_level_instantiate_3d (level_of (f), world, NULL, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (lrg_world3d_get_probe_grids (world)->len, ==, 1);
	grid = g_ptr_array_index (lrg_world3d_get_probe_grids (world), 0);
	lrg_probe_grid_get_bounds (grid, mn, mx);
	lrg_probe_grid_get_resolution (grid, &n[0], &n[1], &n[2]);
	g_assert_cmpfloat_with_epsilon (mn[0], -3.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (mx[2], 3.0f, 1e-4f);
	g_assert_cmpuint (n[0], ==, 5);   /* the node's spacing 1.5 wins */
	g_assert_cmpuint (lrg_probe_grid_get_n_valid (grid), ==, 0);
	/* unbaked: never picked for drawing */
	{
		const gfloat c[3] = { 0.0f, 1.5f, 0.0f };

		g_assert_null (lrg_world3d_find_probe_grid (world, c));
	}

	/* resolution overrides the spacing */
	{
		LrgNode *vol = lrg_editor_find_node (f->editor, "Probes");
		gfloat   w[16];

		g_assert_nonnull (vol);
		lrg_node_visual_set_param_double (lrg_node_get_visual (vol), "resolution", 3.0);
		lrg_editor_node_get_world_matrix (vol, w);
		g_assert_true (lrg_node_visual_get_probe_volume (lrg_node_get_visual (vol), w, 1.0f, mn, mx, n));
		g_assert_cmpuint (n[0], ==, 3);
		g_assert_cmpuint (n[1], ==, 3);
		g_assert_false (lrg_node_visual_get_probe_volume (
			lrg_node_get_visual (lrg_editor_find_node (f->editor, "Box")), w, 1.0f, mn, mx, n));
	}

	/* a level with baked grids does not add the volumes again */
	lrg_level_add_probe_ref (level_of (f), "missing.rprobes");
	g_test_expect_message (NULL, G_LOG_LEVEL_WARNING, "*probe grid*did not load*");
	g_assert_true (lrg_level_instantiate_3d (level_of (f), world2, NULL, &error));
	g_test_assert_expected_messages ();
	g_assert_cmpuint (lrg_world3d_get_probe_grids (world2)->len, ==, 0);
}

/* The inspector's lighting rows and the probe volume's rows and gizmo. */
static void
test_inspector_rows (EditorAppFixture *f,
                     gconstpointer     data)
{
	LrgNode                        *box, *light, *volume;
	g_autoptr(LrgEditorNodeSource)  src_box = NULL;
	g_autoptr(LrgEditorNodeSource)  src_light = NULL;
	g_autoptr(LrgEditorNodeSource)  src_vol = NULL;
	g_autoptr(GPtrArray)            props = NULL;
	gboolean                        has_static = FALSE, has_scale = FALSE, has_mode = FALSE;
	gboolean                        has_ext = FALSE, has_spacing = FALSE;
	guint                           i;

	build_scene (f, FALSE);
	box = lrg_editor_find_node (f->editor, "Box");
	light = lrg_editor_find_node (f->editor, "Lamp");
	volume = lrg_editor_find_node (f->editor, "Probes");

	src_box = lrg_editor_node_source_new (f->editor, box, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);
	props = lrg_property_source_list_properties (LRG_PROPERTY_SOURCE (src_box));
	for (i = 0; i < props->len; i++)
	{
		LrgPropertyInfo *info = g_ptr_array_index (props, i);

		if (g_strcmp0 (lrg_property_info_get_label (info), "Static lighting") == 0)
		{
			has_static = TRUE;
			g_assert_cmpstr (lrg_property_info_get_group (info), ==, "Lighting");
			g_assert_true (lrg_property_info_get_pspec (info)->value_type == G_TYPE_BOOLEAN);
		}
		if (g_strcmp0 (lrg_property_info_get_label (info), "Lightmap scale") == 0)
			has_scale = TRUE;
	}
	g_assert_true (has_static);
	g_assert_true (has_scale);
	g_clear_pointer (&props, g_ptr_array_unref);

	src_light = lrg_editor_node_source_new (f->editor, light, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);
	props = lrg_property_source_list_properties (LRG_PROPERTY_SOURCE (src_light));
	for (i = 0; i < props->len; i++)
		if (g_strcmp0 (lrg_property_info_get_label (g_ptr_array_index (props, i)), "Bake mode") == 0)
		{
			has_mode = TRUE;
			g_assert_cmpstr (lrg_property_info_get_hint (g_ptr_array_index (props, i)), ==,
			                 "enum-strings:realtime,mixed,baked");
		}
	g_assert_true (has_mode);
	g_clear_pointer (&props, g_ptr_array_unref);

	src_vol = lrg_editor_node_source_new (f->editor, volume, LRG_EDITOR_NODE_SECTION_VISUAL, NULL);
	props = lrg_property_source_list_properties (LRG_PROPERTY_SOURCE (src_vol));
	for (i = 0; i < props->len; i++)
	{
		const gchar *label = lrg_property_info_get_label (g_ptr_array_index (props, i));

		has_ext |= g_strcmp0 (label, "Extents X") == 0;
		has_spacing |= g_strcmp0 (label, "Spacing") == 0;
	}
	g_assert_true (has_ext);
	g_assert_true (has_spacing);

	/* the volume gizmo resizes the box with the extent handles */
	g_assert_true (lrg_editor_volume_has_gizmo (volume));
	{
		LrgEditorView view;
		gfloat        p[3];

		lrg_editor_view_init_default (&view, 800.0f, 600.0f);
		g_assert_true (lrg_editor_volume_get_handle_position (&view, volume, LRG_EDITOR_VOLUME_HANDLE_EXTENT_X, p));
		g_assert_cmpfloat_with_epsilon (p[0], 3.0f, 1e-4f);
		g_assert_false (lrg_editor_volume_get_handle_position (&view, volume, LRG_EDITOR_VOLUME_HANDLE_RANGE, p));
	}

	/* Create > Probe Volume makes the node kind */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'node.create','params':"
		                                           "{'name':'Vol2','kind':'probe-volume'}}");

		g_assert_nonnull (lrg_editor_find_node (f->editor, "Vol2"));
		g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (
			lrg_editor_find_node (f->editor, "Vol2"))), ==, LRG_NODE_VISUAL_PROBE_VOLUME);
	}
}

/* Light at cursor: the viewport centre ray hits the floor. */
static void
test_cursor (EditorAppFixture *f,
             gconstpointer     data)
{
	LrgEditorBakePanel *panel;
	gfloat              p[3], n[3], rgb[3];

	build_scene (f, FALSE);
	panel = bake_panel (f);
	fast_settings (panel);
	/* look straight down at the floor from above */
	{
		LrgEditorViewState  *vs = lrg_editor_app_get_view_state (f->app);
		LrgEditorViewCamera *cam = lrg_editor_view_state_get_camera (vs, -1);
		LrgEditorView        v;

		g_assert_nonnull (cam);
		lrg_editor_view_state_get_view (vs, -1, &v);
		v.position[0] = -1.5f;
		v.position[1] = 4.0f;
		v.position[2] = 1.6f;
		v.target[0] = -1.5f;
		v.target[1] = 0.0f;
		v.target[2] = 1.5f;
		lrg_editor_view_camera_from_view (cam, &v);
		lrg_editor_view_state_camera_changed (vs, -1);
	}
	efx_frames (f, 2);
	g_assert_true (lrg_editor_bake_panel_sample_cursor (panel, p, n, rgb));
	g_assert_cmpfloat (n[1], >, 0.9f);
	g_assert_cmpfloat_with_epsilon (p[1], 0.0f, 0.01f);
	g_assert_cmpfloat (rgb[0] + rgb[1] + rgb[2], >, 0.0f);
	efx_click_widget (f, efx_find (f, "bake-sample"));
}

/* ==========================================================================
 * GPU: the authoring viewport with the baked lighting (xrun.sh only)
 * ========================================================================== */

#define GPU_W (160)
#define GPU_H (120)

static Image
render_image (LrgEditorSceneRenderer *renderer,
              const LrgEditorView    *view)
{
	RenderTexture2D rt = LoadRenderTexture (GPU_W, GPU_H);
	Image           img;

	BeginTextureMode (rt);
	g_assert_true (lrg_editor_scene_renderer_render_view (renderer, view, FALSE, 0, LRG_EDITOR_RENDER_MODE_LIT,
	                                                      LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING, GPU_W, GPU_H));
	EndTextureMode ();
	img = LoadImageFromTexture (rt.texture);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	UnloadRenderTexture (rt);
	return img;
}

static guint64
image_diff (Image a,
            Image b)
{
	const guint8 *pa = a.data;
	const guint8 *pb = b.data;
	guint64       sum = 0;
	gint          i;

	for (i = 0; i < a.width * a.height * 4; i++)
		sum += (guint64) ABS ((gint) pa[i] - (gint) pb[i]);
	return sum;
}

/* Distinct colours inside the image's middle (a flat floor would have
 * few: the lightmap must vary over it). */
static guint
distinct_colours (Image img)
{
	g_autoptr(GHashTable) seen = g_hash_table_new (g_direct_hash, g_direct_equal);
	const guint8         *p = img.data;
	gint                  x, y;

	for (y = img.height / 2; y < img.height; y++)
		for (x = img.width / 4; x < img.width * 3 / 4; x++)
		{
			const guint8 *c = &p[(y * img.width + x) * 4];

			g_hash_table_add (seen, GUINT_TO_POINTER (((guint) c[0] << 16) | ((guint) c[1] << 8) | c[2]));
		}
	return g_hash_table_size (seen);
}

static void
test_gpu_viewport (EditorAppFixture *f,
                   gconstpointer     data)
{
	LrgEditorBakePanel     *panel;
	LrgEditorSceneRenderer *sr;
	LrgEditorView           view;
	Image                   baked, plain;
	const gchar            *shots = g_getenv ("LRG_TEST_SHOTS");

	if (!gpu_ready)
	{
		g_test_skip ("no display (run under xrun.sh)");
		return;
	}
	build_scene (f, TRUE);
	panel = bake_panel (f);
	fast_settings (panel);
	g_assert_true (lrg_editor_bake_panel_bake (panel, NULL));
	wait_bake (f, panel);
	g_assert_cmpstr (lrg_editor_bake_panel_get_phase (panel), ==, "done");

	sr = lrg_editor_app_get_scene_renderer (f->app);
	lrg_editor_view_init_default (&view, GPU_W, GPU_H);
	view.position[0] = 4.0f;
	view.position[1] = 4.0f;
	view.position[2] = 5.0f;
	view.target[0] = 0.0f;
	view.target[1] = 0.3f;
	view.target[2] = 0.0f;

	lrg_editor_scene_renderer_set_show_baked (sr, TRUE);
	baked = render_image (sr, &view);
	lrg_editor_scene_renderer_set_show_baked (sr, FALSE);
	plain = render_image (sr, &view);
	lrg_editor_scene_renderer_set_show_baked (sr, TRUE);

	if (shots != NULL)
	{
		g_autofree gchar *a = g_build_filename (shots, "bake-viewport-baked.png", NULL);
		g_autofree gchar *b = g_build_filename (shots, "bake-viewport-plain.png", NULL);

		ExportImage (baked, a);
		ExportImage (plain, b);
	}
	/* the baked light changes the picture, and varies over the floor
	 * (the unwrapped uv2: without it every primitive is one texel) */
	g_assert_cmpuint (image_diff (baked, plain), >, (guint64) GPU_W * GPU_H);
	g_assert_cmpuint (distinct_colours (baked), >, 24);

	UnloadImage (baked);
	UnloadImage (plain);
}

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-editor-app-bake");
	return IsWindowReady ();
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	gpu_ready = gpu_init ();

	g_test_add ("/editor-app/bake/settings", EditorAppFixture, NULL, efx_setup, test_settings, efx_teardown);
	g_test_add ("/editor-app/bake/bake-save", EditorAppFixture, NULL, efx_setup, test_bake_save, efx_teardown);
	g_test_add ("/editor-app/bake/unsaved", EditorAppFixture, NULL, efx_setup, test_unsaved, efx_teardown);
	g_test_add ("/editor-app/bake/cancel-errors", EditorAppFixture, NULL, efx_setup, test_cancel_and_errors,
	            efx_teardown);
	g_test_add ("/editor-app/bake/ipc", EditorAppFixture, NULL, efx_setup, test_ipc, efx_teardown);
	g_test_add ("/editor-app/bake/probe-volume-instantiate", EditorAppFixture, NULL, efx_setup,
	            test_probe_volume_instantiate, efx_teardown);
	g_test_add ("/editor-app/bake/inspector-rows", EditorAppFixture, NULL, efx_setup, test_inspector_rows,
	            efx_teardown);
	g_test_add ("/editor-app/bake/cursor", EditorAppFixture, NULL, efx_setup, test_cursor, efx_teardown);
	g_test_add ("/editor-app/bake/gpu-viewport", EditorAppFixture, NULL, efx_setup, test_gpu_viewport, efx_teardown);

	result = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return result;
}
