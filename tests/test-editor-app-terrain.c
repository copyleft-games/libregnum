/* test-editor-app-terrain.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Terrain and water editing in lrgeditor (0.9.0 M6/M7): Create > Terrain /
 * Water (terrain.create), the shared store and the scene renderer's
 * stand-in components, scripted strokes (terrain.stroke) with one undo
 * step each, Ctrl / Shift modifiers, surface and foliage layers (with
 * undo of structural changes and merged grid gestures), the Terrain
 * panel's widgets (tool strip, lists, empty state), the viewport brush
 * (a drag on the canvas is one stroke, [ ] radius, Escape cancels), the
 * water outline handles (drag, Ctrl+click, Shift+click), saving with the
 * level, the .rwater data editor (fields, waves, points, the store
 * following the unsaved document) and the water.* methods on files and
 * nodes. Every test project is built in the test. GPU-only checks (a real
 * render of terrain and water) run under the isolated Xvfb wrapper.
 */

#include "editor-app-fixture.h"
#include <raylib.h>
#include <math.h>

static gboolean gpu_ready = FALSE;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* The panel, docked as a tab next to the inspector when the shell does
 * not dock it yet. */
static LrgEditorTerrainPanel *
terr_panel (EditorAppFixture *f)
{
	LrgEditorTerrainPanel *panel = lrg_editor_terrain_panel_find (f->app);

	if (panel == NULL)
	{
		LrgDockPanel *dock = lrg_dock_panel_new (LRG_EDITOR_TERRAIN_PANEL_ID, "Terrain");

		panel = lrg_editor_terrain_panel_new (f->app);
		lrg_dock_panel_set_child (dock, LRG_WIDGET (panel));
		g_object_unref (panel);
		lrg_editor_app_add_panel (f->app, dock, "tab:inspector");
	}
	efx_frames (f, 2);
	return panel;
}

static void
present (EditorAppFixture *f)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *p = lrg_dock_area_get_panel (dock, LRG_EDITOR_TERRAIN_PANEL_ID);

	if (p != NULL)
		lrg_dock_area_present_panel (dock, p);
	efx_frames (f, 3);
}

static guint
undo_count (EditorAppFixture *f)
{
	return lrg_editor_get_undo_count (f->editor);
}

/* A project, a level and a terrain node made by terrain.create. */
static LrgNode *
terrain_level (EditorAppFixture *f)
{
	g_autoptr(GError)   error = NULL;
	g_autoptr(JsonNode) r = NULL;
	LrgNode            *node;

	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Terrain", &error));
	g_assert_no_error (error);
	lrg_editor_new_level (f->editor, "land");
	terr_panel (f);
	r = efx_ipc_result (f, "{'id':1,'method':'terrain.create','params':{'name':'Ground'}}");
	node = lrg_editor_find_node (f->editor, json_object_get_string_member (json_node_get_object (r), "guid"));
	g_assert_nonnull (node);
	lrg_editor_select (f->editor, node, FALSE);
	efx_frames (f, 2);
	return node;
}

static LrgTerrainAsset *
asset_of (EditorAppFixture *f,
          LrgNode          *node)
{
	g_autofree gchar *path = lrg_editor_terrain_node_path (f->editor, NULL, node);

	if (path == NULL)
	{
		LrgEditorSceneRenderer *r = lrg_editor_app_get_scene_renderer (f->app);

		path = lrg_editor_terrain_node_path (f->editor, lrg_editor_scene_renderer_get_runtime_renderer (r), node);
	}
	g_assert_nonnull (path);
	return lrg_editor_terrain_store_get_terrain (f->editor, path, NULL);
}

static gfloat
height_at (LrgTerrainAsset *asset,
           gfloat           x,
           gfloat           z)
{
	gfloat h = -999.0f;

	g_assert_true (lrg_terrain_asset_sample_height (asset, x, z, &h));
	return h;
}

static JsonObject *
result_object (JsonNode *n)
{
	return json_node_get_object (n);
}

/* The shell's viewport split (or one the test installs). */
static LrgEditorViewportSplit *
fx_split (EditorAppFixture *f,
          LrgWidget       **old_panel)
{
	LrgWidget              *found = efx_find (f, "viewport-split");
	LrgDockPanel           *dock;
	LrgEditorViewportSplit *split;

	*old_panel = NULL;
	if (found != NULL)
	{
		efx_frames (f, 3);
		return LRG_EDITOR_VIEWPORT_SPLIT (found);
	}
	dock = lrg_dock_area_get_panel (lrg_editor_app_get_dock (f->app), "viewport");
	g_assert_nonnull (dock);
	*old_panel = g_object_ref (lrg_dock_panel_get_child (dock));
	split = lrg_editor_viewport_split_new (f->app);
	lrg_dock_panel_set_child (dock, LRG_WIDGET (split));
	g_object_unref (split);
	efx_frames (f, 3);
	return split;
}

/* Viewport pixels -> canvas. */
static void
vp_to_canvas (EditorAppFixture *f,
              gfloat            u,
              gfloat            v,
              gfloat           *x,
              gfloat           *y)
{
	LrgEditorView view;
	gfloat        rx, ry, rw, rh;

	efx_viewport_rect (f, &rx, &ry, &rw, &rh);
	lrg_editor_view_state_get_view (lrg_editor_app_get_view_state (f->app), 0, &view);
	*x = rx + u * rw / view.width;
	*y = ry + v * rh / view.height;
}

/* A perspective camera above the origin looking down at it. */
static void
camera_above (EditorAppFixture *f,
              gfloat            height)
{
	LrgEditorViewState  *vs = lrg_editor_app_get_view_state (f->app);
	LrgEditorViewCamera *cam = lrg_editor_view_state_get_camera (vs, 0);

	cam->view.orthographic = FALSE;
	cam->view_2d = FALSE;
	cam->view.position[0] = 0.0f;
	cam->view.position[1] = height;
	cam->view.position[2] = height * 0.6f;
	cam->view.target[0] = 0.0f;
	cam->view.target[1] = 0.0f;
	cam->view.target[2] = 0.0f;
	cam->view.up[0] = 0.0f;
	cam->view.up[1] = 1.0f;
	cam->view.up[2] = 0.0f;
	lrg_editor_view_state_camera_changed (vs, 0);
	efx_frames (f, 2);
}

/* Canvas point of a world point in viewport 0. */
static void
world_to_canvas (EditorAppFixture *f,
                 const gfloat     *world,
                 gfloat           *x,
                 gfloat           *y)
{
	LrgEditorView view;
	gfloat        u, v;

	lrg_editor_view_state_get_view (lrg_editor_app_get_view_state (f->app), 0, &view);
	g_assert_true (lrg_editor_view_project (&view, world, &u, &v, NULL));
	vp_to_canvas (f, u, v, x, y);
}

/* ==========================================================================
 * Create, store, renderer
 * ========================================================================== */

/* terrain.create writes a flat .rterrain with two layers, points a new
 * node at it (one undo step) and centres it; the store and the renderer's
 * stand-in share one asset. */
static void
test_create (EditorAppFixture *f,
             gconstpointer     data)
{
	LrgNode               *node = terrain_level (f);
	LrgTerrainAsset       *asset = asset_of (f, node);
	LrgEditorSceneRenderer *renderer = lrg_editor_app_get_scene_renderer (f->app);
	LrgWorld3DRenderer    *resolver = lrg_editor_scene_renderer_get_runtime_renderer (renderer);
	g_autoptr(JsonNode)    r = NULL;
	g_autofree gchar      *path = NULL;
	LrgComponent          *comp;
	GrlVector3            *loc;
	gfloat                 hit[3], local[3];
	gfloat                 o[3] = { 0.0f, 50.0f, 0.0f }, d[3] = { 0.0f, -1.0f, 0.0f };
	guint                  cx, cz;

	g_assert_nonnull (asset);
	g_assert_cmpstr (lrg_node_visual_get_asset (lrg_node_get_visual (node)), ==, "terrain/ground.rterrain");
	path = lrg_editor_terrain_node_path (f->editor, resolver, node);
	g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));
	lrg_terrain_asset_get_chunks (asset, &cx, &cz);
	g_assert_cmpuint (cx, ==, 4);
	g_assert_cmpuint (cz, ==, 4);
	g_assert_cmpuint (lrg_terrain_asset_get_n_layers (asset), ==, 2);
	loc = lrg_node_get_location (node);
	g_assert_cmpfloat_with_epsilon (loc->x, -64.0f, 1e-4f);
	g_assert_cmpfloat_with_epsilon (loc->z, -64.0f, 1e-4f);

	/* one store asset for everyone */
	g_assert_true (lrg_editor_terrain_store_get_terrain (f->editor, path, NULL) == asset);
	comp = lrg_editor_terrain_node_component (f->editor, resolver, node);
	g_assert_nonnull (comp);
	g_assert_true (LRG_IS_TERRAIN_COMPONENT (comp));
	g_assert_true (lrg_terrain_component_get_asset (LRG_TERRAIN_COMPONENT (comp)) == asset);
	g_assert_true (lrg_editor_terrain_node_component (f->editor, resolver, node) == comp);
	g_assert_false (lrg_editor_scene_renderer_is_asset_missing (renderer, node));

	/* the stand-in follows the node: a ray at the world origin hits the
	 * terrain at its local centre */
	g_assert_true (lrg_terrain_component_raycast (LRG_TERRAIN_COMPONENT (comp), o, d, 100.0f, hit, local));
	g_assert_cmpfloat_with_epsilon (local[0], 64.0f, 1e-2f);
	g_assert_cmpfloat_with_epsilon (local[2], 64.0f, 1e-2f);
	lrg_node_set_location_xyz (node, -60.0f, 0.0f, -64.0f);
	comp = lrg_editor_terrain_node_component (f->editor, resolver, node);
	g_assert_true (lrg_terrain_component_raycast (LRG_TERRAIN_COMPONENT (comp), o, d, 100.0f, hit, local));
	g_assert_cmpfloat_with_epsilon (local[0], 60.0f, 1e-2f);
	lrg_node_set_location_xyz (node, -64.0f, 0.0f, -64.0f);

	/* terrain.get */
	r = efx_ipc_result (f, "{'id':1,'method':'terrain.get'}");
	g_assert_cmpint (json_object_get_int_member (result_object (r), "resolution"), ==, 32);
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (result_object (r), "layers")), ==, 2);
	g_assert_cmpstr (json_object_get_string_member (json_object_get_object_member (result_object (r), "brush"), "tool"),
	                 ==, "select");
	g_assert_false (json_object_get_boolean_member (result_object (r), "dirty"));

	/* undo takes the node away (the file stays) */
	lrg_editor_undo (f->editor);
	g_assert_null (lrg_editor_find_node (f->editor, "Ground"));
	lrg_editor_redo (f->editor);
	g_assert_nonnull (lrg_editor_find_node (f->editor, "Ground"));

	/* a second terrain gets its own file; a broken asset is missing */
	{
		g_autoptr(JsonNode) r2 = efx_ipc_result (f, "{'id':1,'method':'terrain.create','params':{'name':'Ground'}}");
		LrgNode            *other;

		g_assert_cmpstr (json_object_get_string_member (result_object (r2), "asset"), ==, "terrain/ground2.rterrain");
		other = lrg_editor_find_node (f->editor, json_object_get_string_member (result_object (r2), "guid"));
		lrg_node_visual_set_asset (lrg_node_get_visual (other), "terrain/nope.rterrain");
		g_assert_true (lrg_editor_scene_renderer_is_asset_missing (renderer, other));
		g_assert_null (lrg_editor_terrain_node_component (f->editor, resolver, other));
	}

	/* the Create actions exist */
	g_assert_true (lrg_editor_app_activate_action (f->app, "terrain.create-water"));
	g_assert_nonnull (lrg_editor_find_node (f->editor, "Water"));
}

/* ==========================================================================
 * Strokes
 * ========================================================================== */

static void
test_stroke (EditorAppFixture *f,
             gconstpointer     data)
{
	LrgNode             *node = terrain_level (f);
	LrgTerrainAsset     *asset = asset_of (f, node);
	g_autoptr(JsonNode)  r = NULL;
	g_autofree gchar    *path = NULL;
	guint                undo;
	gfloat               h0;

	path = lrg_editor_terrain_node_path (f->editor,
	                                     lrg_editor_scene_renderer_get_runtime_renderer (
	                                         lrg_editor_app_get_scene_renderer (f->app)), node);

	/* a world-space raise at the origin: local (64, 64) goes up by the
	 * strength; one undo step labelled after the tool */
	undo = undo_count (f);
	r = efx_ipc_result (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'raise','points':[[0,0]],"
	                       "'strength':5,'radius':6}}");
	g_assert_true (json_object_get_boolean_member (result_object (r), "changed"));
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "label"), ==, "Terrain: Raise stroke");
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	h0 = height_at (asset, 64.0f, 64.0f);
	g_assert_cmpfloat_with_epsilon (h0, 5.0f, 0.05f);
	g_assert_cmpfloat_with_epsilon (height_at (asset, 80.0f, 64.0f), 0.0f, 1e-4f);
	g_assert_true (lrg_editor_terrain_store_is_dirty (f->editor, path));

	/* the brush overrides were for that stroke only */
	g_assert_cmpfloat_with_epsilon (lrg_terrain_brush_get_radius (
		lrg_editor_terrain_panel_get_brush (lrg_editor_terrain_panel_find (f->app))), 8.0f, 1e-5f);

	/* undo / redo restore the region */
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat_with_epsilon (height_at (asset, 64.0f, 64.0f), 0.0f, 1e-4f);
	lrg_editor_redo (f->editor);
	g_assert_cmpfloat_with_epsilon (height_at (asset, 64.0f, 64.0f), h0, 1e-4f);

	/* several local points are still one step; invert (Ctrl) lowers */
	undo = undo_count (f);
	efx_ipc_result (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'raise','local':true,"
	                   "'points':[[20,20],[24,20],[28,20]],'strength':2,'invert':true}}");
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	g_assert_cmpfloat (height_at (asset, 24.0f, 20.0f), <, -1.0f);

	/* smooth (Shift) pulls the peak down */
	efx_ipc_result (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'raise','points':[[0,0]],"
	                   "'smooth':true,'radius':4,'amount':4}}");
	g_assert_cmpfloat (height_at (asset, 64.0f, 64.0f), <, h0);

	/* flatten to a target */
	efx_ipc_result (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'flatten','local':true,"
	                   "'points':[[100,100]],'target_height':3,'strength':100,'falloff':'constant','radius':3}}");
	g_assert_cmpfloat_with_epsilon (height_at (asset, 100.0f, 100.0f), 3.0f, 0.01f);

	/* paint layer 1 (rock) */
	efx_ipc_result (f, "{'id':1,'method':'terrain.brush','params':{'layer':1}}");
	efx_ipc_result (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'paint','local':true,"
	                   "'points':[[40,90]],'strength':100,'radius':3}}");
	g_assert_cmpfloat (lrg_terrain_asset_get_weight (asset, 40, 90, 1), >, 0.9f);

	/* holes and fill */
	efx_ipc_result (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'hole','local':true,"
	                   "'points':[[10,110]],'radius':2}}");
	g_assert_cmpuint (lrg_terrain_asset_get_hole_count (asset), >, 0);
	efx_ipc_result (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'hole','invert':true,'local':true,"
	                   "'points':[[10,110]],'radius':3}}");
	g_assert_cmpuint (lrg_terrain_asset_get_hole_count (asset), ==, 0);

	/* terrain.sample */
	r = efx_ipc_result (f, "{'id':1,'method':'terrain.sample','params':{'x':100,'z':100,'local':true}}");
	g_assert_true (json_object_get_boolean_member (result_object (r), "hit"));
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (result_object (r), "height"), 3.0, 0.01);

	/* errors: no tool, nothing on the terrain, unknown tool */
	{
		g_autofree gchar *e1 = efx_ipc (f, "{'id':1,'method':'terrain.stroke','params':{'points':[[0,0]]}}");
		g_autofree gchar *e2 = efx_ipc (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'raise',"
		                                   "'points':[[900,900]]}}");
		g_autofree gchar *e3 = efx_ipc (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'dig',"
		                                   "'points':[[0,0]]}}");

		g_assert_nonnull (strstr (e1, "\"error\""));
		g_assert_nonnull (strstr (e2, "\"error\""));
		g_assert_nonnull (strstr (e3, "\"error\""));
	}

	/* terrain.save writes the file; a fresh load matches */
	r = efx_ipc_result (f, "{'id':1,'method':'terrain.save'}");
	g_assert_false (lrg_editor_terrain_store_is_dirty (f->editor, path));
	{
		g_autoptr(LrgTerrainAsset) loaded = lrg_terrain_asset_new_from_file (path, NULL);

		g_assert_nonnull (loaded);
		g_assert_cmpfloat_with_epsilon (height_at (loaded, 100.0f, 100.0f), 3.0f, 0.02f);
	}
}

/* ==========================================================================
 * Layers and foliage
 * ========================================================================== */

static void
gv_double (GValue  *v,
           gdouble  d)
{
	g_value_init (v, G_TYPE_DOUBLE);
	g_value_set_double (v, d);
}

static void
test_layers (EditorAppFixture *f,
             gconstpointer     data)
{
	LrgNode               *node = terrain_level (f);
	LrgEditorTerrainPanel *panel = lrg_editor_terrain_panel_find (f->app);
	LrgTerrainAsset       *asset = asset_of (f, node);
	LrgFoliage            *foliage = lrg_terrain_asset_get_foliage (asset);
	g_autoptr(JsonNode)    r = NULL;
	guint                  undo;
	gint                   index;

	/* add a layer: selected, one step; paint with it, then remove it:
	 * undo brings the layer and its weights back */
	undo = undo_count (f);
	index = lrg_editor_terrain_panel_add_layer (panel, NULL, "sand");
	g_assert_cmpint (index, ==, 2);
	g_assert_cmpint (lrg_editor_terrain_panel_get_selected_layer (panel), ==, 2);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	efx_ipc_result (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'paint','local':true,"
	                   "'points':[[50,50]],'strength':100,'radius':3}}");
	g_assert_cmpfloat (lrg_terrain_asset_get_weight (asset, 50, 50, 2), >, 0.9f);
	g_assert_true (lrg_editor_terrain_panel_remove_layer (panel, NULL, 2));
	g_assert_cmpuint (lrg_terrain_asset_get_n_layers (asset), ==, 2);
	g_assert_cmpfloat (lrg_terrain_asset_get_weight (asset, 50, 50, 2), <, 0.01f);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_terrain_asset_get_n_layers (asset), ==, 3);
	g_assert_cmpstr (lrg_terrain_layer_get_name (lrg_terrain_asset_get_layer (asset, 2)), ==, "sand");
	g_assert_cmpfloat (lrg_terrain_asset_get_weight (asset, 50, 50, 2), >, 0.9f);
	lrg_editor_redo (f->editor);
	g_assert_cmpuint (lrg_terrain_asset_get_n_layers (asset), ==, 2);
	lrg_editor_undo (f->editor);

	/* fields: one step each; a grid gesture merges */
	{
		g_auto(GValue) v = G_VALUE_INIT;
		LrgPropertyGrid *grid = lrg_editor_terrain_panel_get_layer_grid (panel);

		gv_double (&v, 9.0);
		undo = undo_count (f);
		g_assert_true (lrg_editor_terrain_panel_set_layer_field (panel, NULL, 1, "tiling", &v));
		g_assert_cmpfloat_with_epsilon (lrg_terrain_layer_get_tiling (lrg_terrain_asset_get_layer (asset, 1)), 9.0f,
		                                1e-5f);
		g_assert_cmpuint (undo_count (f), ==, undo + 1);
		g_signal_emit_by_name (grid, "edit-begin", "tiling", 7u);
		g_value_set_double (&v, 10.0);
		g_assert_true (lrg_editor_terrain_panel_set_layer_field (panel, NULL, 1, "tiling", &v));
		g_value_set_double (&v, 11.0);
		g_assert_true (lrg_editor_terrain_panel_set_layer_field (panel, NULL, 1, "tiling", &v));
		g_signal_emit_by_name (grid, "edit-end", "tiling", 7u, TRUE);
		g_assert_cmpuint (undo_count (f), ==, undo + 2);
		lrg_editor_undo (f->editor);
		g_assert_cmpfloat_with_epsilon (lrg_terrain_layer_get_tiling (lrg_terrain_asset_get_layer (asset, 1)), 9.0f,
		                                1e-5f);
		lrg_editor_redo (f->editor);
		g_assert_cmpfloat_with_epsilon (lrg_terrain_layer_get_tiling (lrg_terrain_asset_get_layer (asset, 1)), 11.0f,
		                                1e-5f);
	}

	/* IPC: set_layer with a colour, add_layer with fields (one step) */
	efx_ipc_result (f, "{'id':1,'method':'terrain.set_layer','params':{'index':0,'name':'meadow',"
	                   "'color':[0.2,0.6,0.2,1]}}");
	g_assert_cmpstr (lrg_terrain_layer_get_name (lrg_terrain_asset_get_layer (asset, 0)), ==, "meadow");
	{
		gfloat c[4];

		lrg_terrain_layer_get_color (lrg_terrain_asset_get_layer (asset, 0), c);
		g_assert_cmpfloat_with_epsilon (c[1], 0.6f, 0.01f);
	}
	undo = undo_count (f);
	r = efx_ipc_result (f, "{'id':1,'method':'terrain.add_layer','params':{'name':'snow','tiling':3}}");
	g_assert_cmpint (json_object_get_int_member (result_object (r), "index"), ==, 3);   /* sand is back */
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_layer_get_tiling (lrg_terrain_asset_get_layer (asset, 3)), 3.0f, 1e-5f);
	{
		g_autofree gchar *e = efx_ipc (f, "{'id':1,'method':'terrain.remove_layer','params':{'index':7}}");

		g_assert_nonnull (strstr (e, "\"error\""));
	}

	/* foliage: add (selected), paint density, set rules, remove + undo */
	undo = undo_count (f);
	index = lrg_editor_terrain_panel_add_foliage (panel, NULL, "pines", LRG_FOLIAGE_KIND_MESH);
	g_assert_cmpint (index, ==, 0);
	g_assert_cmpint (lrg_editor_terrain_panel_get_selected_foliage (panel), ==, 0);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	efx_ipc_result (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'foliage-erase','local':true,"
	                   "'points':[[30,30]],'strength':100,'radius':4}}");
	g_assert_cmpfloat (lrg_foliage_get_density (foliage, 0, 30, 30), <, 0.1f);
	efx_ipc_result (f, "{'id':1,'method':'terrain.set_foliage','params':{'index':0,'density':0.05,"
	                   "'slope':[0,30],'height':[-5,50],'seed':7}}");
	{
		gfloat a, b;

		g_assert_cmpfloat_with_epsilon (lrg_foliage_layer_get_density (lrg_foliage_get_layer (foliage, 0)), 0.05f,
		                                1e-5f);
		lrg_foliage_layer_get_slope_range (lrg_foliage_get_layer (foliage, 0), &a, &b);
		g_assert_cmpfloat_with_epsilon (b, 30.0f, 1e-4f);
		g_assert_cmpuint (lrg_foliage_layer_get_seed (lrg_foliage_get_layer (foliage, 0)), ==, 7);
	}
	g_assert_cmpuint (lrg_editor_terrain_panel_rebake (panel, NULL), >, 0);
	r = efx_ipc_result (f, "{'id':1,'method':'terrain.add_foliage','params':{'name':'grass','kind':'grass'}}");
	g_assert_cmpint (json_object_get_int_member (result_object (r), "index"), ==, 1);
	g_assert_true (lrg_editor_terrain_panel_remove_foliage (panel, NULL, 0));
	g_assert_cmpuint (lrg_foliage_get_n_layers (foliage), ==, 1);
	g_assert_cmpstr (lrg_foliage_layer_get_name (lrg_foliage_get_layer (foliage, 0)), ==, "grass");
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_foliage_get_n_layers (foliage), ==, 2);
	g_assert_cmpstr (lrg_foliage_layer_get_name (lrg_foliage_get_layer (foliage, 0)), ==, "pines");
	g_assert_cmpfloat (lrg_foliage_get_density (foliage, 0, 30, 30), <, 0.1f);   /* the map came back */
	g_assert_cmpint (lrg_foliage_layer_get_kind (lrg_foliage_get_layer (foliage, 1)), ==, LRG_FOLIAGE_KIND_GRASS);

	/* terrain.get reports both lists */
	{
		g_autoptr(JsonNode) g = efx_ipc_result (f, "{'id':1,'method':'terrain.get'}");

		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (result_object (g), "foliage")), ==, 2);
		g_assert_true (json_object_get_boolean_member (result_object (g), "dirty"));
	}
}

/* ==========================================================================
 * The panel's widgets
 * ========================================================================== */

static void
test_panel (EditorAppFixture *f,
            gconstpointer     data)
{
	LrgEditorTerrainPanel *panel;
	LrgNode               *node;
	LrgNode               *cube;
	LrgWidget             *tools;
	gfloat                 x, y, w, h;
	guint                  undo;

	node = terrain_level (f);
	panel = lrg_editor_terrain_panel_find (f->app);
	present (f);
	g_assert_true (lrg_editor_terrain_panel_get_node (panel) == node);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "terrain-scroll")));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "terrain-empty")));
	g_assert_cmpint (lrg_editor_terrain_panel_get_selected_layer (panel), ==, 0);

	/* the tool strip: the second cell is Raise */
	tools = efx_find (f, "terrain-tools");
	efx_rect_of (tools, &x, &y, &w, &h);
	g_assert_cmpfloat (w, >, 0.0f);
	{
		/* cells are 58 px or wider, as many per row as fit */
		guint cols = MIN ((guint) (w / 58.0f), 11u);

		efx_click (f, x + (w / (gfloat) cols) * 1.5f, y + 20.0f);
	}
	g_assert_cmpint (lrg_editor_terrain_panel_get_tool (panel), ==, LRG_EDITOR_TERRAIN_TOOL_RAISE);
	g_assert_true (lrg_editor_terrain_panel_get_brush_active (panel));
	g_assert_true (lrg_editor_app_activate_action (f->app, "terrain.tool-smooth"));
	g_assert_cmpint (lrg_editor_terrain_panel_get_tool (panel), ==, LRG_EDITOR_TERRAIN_TOOL_SMOOTH);

	/* Add layer chip; a click on a row selects it.
	 * The chip is below the fold of the inspector split. */
	undo = undo_count (f);
	efx_frames (f, 2);
	lrg_scroll_box_scroll_to_widget (LRG_SCROLL_BOX (efx_find (f, "terrain-scroll")),
	                                 efx_find (f, "terrain-add-layer"));
	efx_frames (f, 40);
	efx_click_widget (f, efx_find (f, "terrain-add-layer"));
	g_assert_cmpuint (lrg_terrain_asset_get_n_layers (lrg_editor_terrain_panel_get_asset (panel)), ==, 3);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	efx_frames (f, 2);
	lrg_scroll_box_scroll_to_widget (LRG_SCROLL_BOX (efx_find (f, "terrain-scroll")),
	                                 efx_find (f, "terrain-layer-list"));
	efx_frames (f, 40);
	efx_rect_of (efx_find (f, "terrain-layer-list"), &x, &y, &w, &h);
	efx_click (f, x + 40.0f, y + 1.0f + 24.0f * 0.5f);
	g_assert_cmpint (lrg_editor_terrain_panel_get_selected_layer (panel), ==, 0);
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (lrg_editor_terrain_panel_get_layer_grid (panel))));

	/* foliage Add chip with the kind picker */
	efx_frames (f, 2);
	lrg_scroll_box_scroll_to_widget (LRG_SCROLL_BOX (efx_find (f, "terrain-scroll")),
	                                 efx_find (f, "terrain-add-foliage"));
	efx_frames (f, 40);
	efx_click_widget (f, efx_find (f, "terrain-add-foliage"));
	g_assert_cmpint (lrg_editor_terrain_panel_get_selected_foliage (panel), ==, 0);
	efx_frames (f, 2);
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (lrg_editor_terrain_panel_get_foliage_grid (panel))));

	/* the summary line */
	{
		g_autofree gchar *line = lrg_editor_terrain_panel_describe (panel);

		g_assert_nonnull (strstr (line, "4 x 4 chunks"));
		g_assert_nonnull (strstr (line, "unsaved"));
	}

	/* another node: the empty state, the brush is off */
	cube = lrg_editor_find_node (f->editor, "Ground");
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'node.create','params':{'name':'Box',"
		                                           "'kind':'primitive','primitive':'cube'}}");

		cube = lrg_editor_find_node (f->editor, json_object_get_string_member (result_object (r), "guid"));
	}
	lrg_editor_select (f->editor, cube, FALSE);
	efx_frames (f, 3);
	g_assert_null (lrg_editor_terrain_panel_get_node (panel));
	g_assert_false (lrg_editor_terrain_panel_get_brush_active (panel));
	g_assert_true (lrg_widget_get_visible (efx_find (f, "terrain-empty")));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "terrain-scroll")));
	lrg_editor_select (f->editor, node, FALSE);
	efx_frames (f, 2);
	g_assert_true (lrg_editor_terrain_panel_get_node (panel) == node);
}

/* ==========================================================================
 * The viewport brush
 * ========================================================================== */

static void
test_viewport_brush (EditorAppFixture *f,
                     gconstpointer     data)
{
	g_autoptr(LrgWidget)   old = NULL;
	LrgEditorViewportSplit *split;
	LrgEditorViewportPanel *vp;
	LrgEditorTerrainPanel  *panel;
	LrgTerrainAsset        *asset;
	LrgNode                *node;
	gfloat                  world[3] = { 0.0f, 0.0f, 0.0f };
	gfloat                  x, y, local[3], r0;
	guint                   undo;

	node = terrain_level (f);
	split = fx_split (f, &old);
	vp = lrg_editor_viewport_split_get_panel (split, 0);
	panel = lrg_editor_terrain_panel_find (f->app);
	asset = asset_of (f, node);
	camera_above (f, 60.0f);
	lrg_editor_terrain_panel_set_tool (panel, LRG_EDITOR_TERRAIN_TOOL_RAISE);
	world_to_canvas (f, world, &x, &y);

	/* hovering puts the cursor on the terrain under the pointer */
	efx_move (f, x, y);
	efx_move (f, x + 1.0f, y);
	g_assert_true (lrg_editor_terrain_panel_get_cursor (panel, local, NULL));
	g_assert_cmpfloat_with_epsilon (local[0], 64.0f, 1.5f);
	g_assert_cmpfloat_with_epsilon (local[2], 64.0f, 1.5f);
	efx_frames (f, 1);
	g_assert_cmpuint (lrg_editor_viewport_panel_get_terrain_painted (vp), >=, 0);

	/* a drag is one stroke: heights rise, one undo step */
	undo = undo_count (f);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x + 20.0f, y, 6);
	g_assert_false (lrg_editor_terrain_panel_is_stroking (panel));
	g_assert_cmpfloat (height_at (asset, 64.0f, 64.0f), >, 0.1f);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat_with_epsilon (height_at (asset, 64.0f, 64.0f), 0.0f, 1e-4f);

	/* Ctrl inverts: the same drag lowers */
	f->mods = LRG_INPUT_MODIFIER_CTRL;
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x, y + 10.0f, 4);
	f->mods = LRG_INPUT_MODIFIER_NONE;
	g_assert_cmpfloat (height_at (asset, 64.0f, 64.0f), <, -0.05f);
	lrg_editor_undo (f->editor);

	/* Escape during a stroke puts the heights back, no step */
	undo = undo_count (f);
	efx_move (f, x, y);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_frames (f, 3);
	g_assert_true (lrg_editor_terrain_panel_is_stroking (panel));
	efx_key (f, GRL_KEY_ESCAPE, LRG_INPUT_MODIFIER_NONE);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_LEFT, FALSE);
	g_assert_cmpfloat_with_epsilon (height_at (asset, 64.0f, 64.0f), 0.0f, 1e-4f);
	g_assert_cmpuint (undo_count (f), ==, undo);

	/* [ and ] scale the radius */
	r0 = lrg_terrain_brush_get_radius (lrg_editor_terrain_panel_get_brush (panel));
	efx_move (f, x, y);
	efx_key (f, GRL_KEY_RIGHT_BRACKET, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpfloat (lrg_terrain_brush_get_radius (lrg_editor_terrain_panel_get_brush (panel)), >, r0);
	efx_key (f, GRL_KEY_LEFT_BRACKET, LRG_INPUT_MODIFIER_NONE);
	g_assert_cmpfloat_with_epsilon (lrg_terrain_brush_get_radius (lrg_editor_terrain_panel_get_brush (panel)), r0,
	                                1e-3f);

	/* the Select tool gives the left button back to the gizmo / picking */
	lrg_editor_terrain_panel_set_tool (panel, LRG_EDITOR_TERRAIN_TOOL_SELECT);
	undo = undo_count (f);
	efx_click (f, x, y);
	g_assert_cmpfloat_with_epsilon (height_at (asset, 64.0f, 64.0f), 0.0f, 1e-4f);
}

/* ==========================================================================
 * Water nodes and the viewport handles
 * ========================================================================== */

static void
test_water_node (EditorAppFixture *f,
                 gconstpointer     data)
{
	g_autoptr(LrgWidget)   old = NULL;
	g_autoptr(GError)      error = NULL;
	g_autoptr(JsonNode)    r = NULL;
	g_autofree gchar      *path = NULL;
	g_autofree gchar      *req = NULL;
	LrgEditorTerrainPanel *panel;
	LrgEditorViewportSplit *split;
	LrgWaterBody          *body;
	LrgNode               *water;
	gfloat                 p[4], w[3], x, y;
	guint                  undo, n;

	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Lake", &error));
	lrg_editor_new_level (f->editor, "lake");
	panel = terr_panel (f);
	split = fx_split (f, &old);
	r = efx_ipc_result (f, "{'id':1,'method':'terrain.create','params':{'kind':'water','name':'Pond'}}");
	water = lrg_editor_find_node (f->editor, json_object_get_string_member (result_object (r), "guid"));
	g_assert_nonnull (water);
	g_assert_cmpstr (lrg_node_visual_get_asset (lrg_node_get_visual (water)), ==, "terrain/pond.rwater");
	path = lrg_editor_terrain_node_path (f->editor,
	                                     lrg_editor_scene_renderer_get_runtime_renderer (
	                                         lrg_editor_app_get_scene_renderer (f->app)), water);
	body = lrg_editor_terrain_store_get_water (f->editor, path, NULL);
	g_assert_nonnull (body);
	g_assert_cmpuint (lrg_water_body_get_n_points (body), ==, 4);
	g_assert_true (LRG_IS_WATER_COMPONENT (lrg_editor_terrain_node_component (
		f->editor, lrg_editor_scene_renderer_get_runtime_renderer (lrg_editor_app_get_scene_renderer (f->app)), water)));

	/* water.* on the node: one level step each */
	g_clear_pointer (&r, json_node_unref);
	req = g_strdup_printf ("{'id':1,'method':'water.get','params':{'node':'%s'}}", lrg_node_get_guid (water));
	r = efx_ipc_result (f, req);
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "shape"), ==, "lake");
	g_assert_cmpuint (json_array_get_length (json_object_get_array_member (result_object (r), "points")), ==, 4);
	undo = undo_count (f);
	g_free (req);
	req = g_strdup_printf ("{'id':1,'method':'water.set','params':{'node':'%s','level':1.5,'wave_scale':0.5}}",
	                       lrg_node_get_guid (water));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, req);
	g_assert_cmpfloat_with_epsilon (lrg_water_body_get_level (body), 1.5f, 1e-5f);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	g_assert_true (lrg_editor_terrain_store_is_dirty (f->editor, path));
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat_with_epsilon (lrg_water_body_get_level (body), 0.5f, 1e-5f);
	lrg_editor_redo (f->editor);
	g_free (req);
	req = g_strdup_printf ("{'id':1,'method':'water.add_point','params':{'node':'%s','x':0,'z':-14,'index':1}}",
	                       lrg_node_get_guid (water));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, req);
	g_assert_cmpint (json_object_get_int_member (result_object (r), "index"), ==, 1);
	g_assert_cmpuint (lrg_water_body_get_n_points (body), ==, 5);
	lrg_water_body_get_point (body, 1, p);
	g_assert_cmpfloat_with_epsilon (p[2], -14.0f, 1e-5f);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_water_body_get_n_points (body), ==, 4);

	/* the viewport handles of the selected water node */
	lrg_editor_select (f->editor, water, FALSE);
	camera_above (f, 40.0f);
	{
		GrlVector3 *loc = lrg_node_get_location (water);

		lrg_water_body_get_point (body, 0, p);
		w[0] = loc->x + p[0];
		w[1] = loc->y + lrg_water_body_get_level (body);
		w[2] = loc->z + p[2];
	}
	world_to_canvas (f, w, &x, &y);
	efx_move (f, x, y);
	efx_frames (f, 1);

	/* drag point 0 by 30 px: it moves, one step */
	undo = undo_count (f);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x + 30.0f, y, 3);
	lrg_water_body_get_point (body, 0, p);
	g_assert_cmpfloat (p[0], >, -9.0f);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	lrg_editor_undo (f->editor);
	lrg_water_body_get_point (body, 0, p);
	g_assert_cmpfloat_with_epsilon (p[0], -10.0f, 1e-4f);

	/* Ctrl+click on the water adds a point, Shift+click on a handle
	 * removes one */
	n = lrg_water_body_get_n_points (body);
	{
		gfloat c[3], cx, cy;
		GrlVector3 *loc = lrg_node_get_location (water);

		c[0] = loc->x + 1.0f;
		c[1] = loc->y + lrg_water_body_get_level (body);
		c[2] = loc->z + 13.0f;
		world_to_canvas (f, c, &cx, &cy);
		f->mods = LRG_INPUT_MODIFIER_CTRL;
		efx_click (f, cx, cy);
		f->mods = LRG_INPUT_MODIFIER_NONE;
	}
	g_assert_cmpuint (lrg_water_body_get_n_points (body), ==, n + 1);
	f->mods = LRG_INPUT_MODIFIER_SHIFT;
	efx_click (f, x, y);
	f->mods = LRG_INPUT_MODIFIER_NONE;
	g_assert_cmpuint (lrg_water_body_get_n_points (body), ==, n);
	lrg_editor_undo (f->editor);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_water_body_get_n_points (body), ==, n);

	/* level save writes the dirty water file */
	{
		g_autofree gchar *level = g_build_filename (f->project, "lake.rlevel", NULL);
		g_autoptr(LrgWaterBody) loaded = NULL;

		g_assert_true (lrg_editor_terrain_store_is_dirty (f->editor, path));
		g_assert_true (lrg_editor_save_level (f->editor, level, &error));
		g_assert_no_error (error);
		g_assert_false (lrg_editor_terrain_store_is_dirty (f->editor, path));
		loaded = lrg_water_body_new_from_file (path, &error);
		g_assert_nonnull (loaded);
		g_assert_cmpfloat_with_epsilon (lrg_water_body_get_level (loaded), 1.5f, 1e-4f);
	}
	(void) panel;
	(void) split;
}

/* ==========================================================================
 * The .rwater data editor
 * ========================================================================== */

static void
test_water_editor (EditorAppFixture *f,
                   gconstpointer     data)
{
	g_autoptr(GError)      error = NULL;
	g_autoptr(JsonNode)    r = NULL;
	g_autofree gchar      *path = NULL;
	g_autofree gchar      *req = NULL;
	LrgEditorDataEditor   *ed;
	LrgEditorWaterEditor  *we;
	LrgWaterBody          *stored;
	g_auto(GValue)         v = G_VALUE_INIT;
	LrgWaterWave           wave = { { 0.0f, 1.0f }, 0.2f, 9.0f, 2.0f, 0.5f };
	gfloat                 pt[4] = { 5.0f, 0.0f, 5.0f, 3.0f };

	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Water", &error));
	lrg_editor_new_level (f->editor, "w");
	terr_panel (f);
	r = efx_ipc_result (f, "{'id':1,'method':'terrain.create','params':{'kind':'water','name':'Lake'}}");
	path = g_build_filename (f->project, "terrain", "lake.rwater", NULL);
	g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));

	/* an unsaved node edit is what the editor opens with */
	{
		g_autofree gchar   *set = g_strdup_printf ("{'id':1,'method':'water.set','params':{'node':'%s','level':1.25}}",
		                                           json_object_get_string_member (result_object (r), "guid"));
		g_autoptr(JsonNode) rs = efx_ipc_result (f, set);
	}
	ed = lrg_editor_data_editor_open (f->app, path, &error);
	g_assert_no_error (error);
	g_assert_true (LRG_IS_EDITOR_WATER_EDITOR (ed));
	we = LRG_EDITOR_WATER_EDITOR (ed);
	efx_frames (f, 3);
	g_assert_nonnull (lrg_editor_water_editor_get_body (we));
	g_assert_cmpfloat_with_epsilon (lrg_water_body_get_level (lrg_editor_water_editor_get_body (we)), 1.25f, 1e-5f);
	g_assert_true (lrg_editor_data_editor_get_dirty (ed));
	g_assert_true (lrg_editor_data_editor_undo (ed));   /* back to the file's 0.5 */
	g_assert_cmpint (lrg_editor_water_editor_get_selected_point (we), ==, 0);
	g_assert_cmpint (lrg_editor_water_editor_get_selected_wave (we), ==, 0);

	/* a field is one document step; the store follows the unsaved text */
	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 2.25);
	g_assert_true (lrg_editor_water_editor_set_field (we, "level", &v, 0, &error));
	g_assert_no_error (error);
	g_assert_cmpfloat_with_epsilon (lrg_water_body_get_level (lrg_editor_water_editor_get_body (we)), 2.25f, 1e-5f);
	g_assert_true (lrg_editor_data_editor_get_dirty (ed));
	stored = lrg_editor_terrain_store_get_water (f->editor, path, NULL);
	g_assert_cmpfloat_with_epsilon (lrg_water_body_get_level (stored), 2.25f, 1e-5f);
	g_assert_true (lrg_editor_data_editor_undo (ed));
	g_assert_cmpfloat_with_epsilon (lrg_water_body_get_level (lrg_editor_water_editor_get_body (we)), 0.5f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (lrg_water_body_get_level (stored), 0.5f, 1e-5f);

	/* colours from strings; shape; unknown fields fail */
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "#204060ff");
	g_assert_true (lrg_editor_water_editor_set_field (we, "deep-color", &v, 0, NULL));
	g_value_set_string (&v, "river");
	g_assert_true (lrg_editor_water_editor_set_field (we, "shape", &v, 0, NULL));
	g_assert_cmpint (lrg_water_body_get_shape (lrg_editor_water_editor_get_body (we)), ==, LRG_WATER_BODY_RIVER);
	g_value_set_string (&v, "x");
	g_assert_false (lrg_editor_water_editor_set_field (we, "bogus", &v, 0, NULL));

	/* waves and points */
	g_assert_true (lrg_editor_water_editor_set_wave_count (we, 3, NULL));
	g_assert_cmpuint (lrg_water_body_get_wave_count (lrg_editor_water_editor_get_body (we)), ==, 3);
	g_assert_true (lrg_editor_water_editor_set_wave (we, 1, &wave, 0, NULL));
	{
		LrgWaterWave got;

		g_assert_true (lrg_water_body_get_wave (lrg_editor_water_editor_get_body (we), 1, &got));
		g_assert_cmpfloat_with_epsilon (got.wavelength, 9.0f, 1e-5f);
	}
	g_assert_cmpint (lrg_editor_water_editor_add_point (we, 2, pt, NULL), ==, 2);
	g_assert_cmpuint (lrg_water_body_get_n_points (lrg_editor_water_editor_get_body (we)), ==, 5);
	g_assert_cmpint (lrg_editor_water_editor_get_selected_point (we), ==, 2);
	pt[3] = 6.0f;
	g_assert_true (lrg_editor_water_editor_set_point (we, 2, pt, 0, NULL));
	g_assert_true (lrg_editor_water_editor_remove_point (we, 0, NULL));
	g_assert_cmpuint (lrg_water_body_get_n_points (lrg_editor_water_editor_get_body (we)), ==, 4);

	/* water.* on the file */
	req = g_strdup_printf ("{'id':1,'method':'water.set_wave','params':{'path':'%s','index':0,'amplitude':0.3}}",
	                       path);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, req);
	g_assert_cmpfloat_with_epsilon (json_object_get_double_member (
		json_array_get_object_element (json_object_get_array_member (result_object (r), "waves"), 0), "amplitude"),
		0.3, 1e-5);
	g_free (req);
	req = g_strdup_printf ("{'id':1,'method':'water.set','params':{'path':'%s','shape':'lake',"
	                       "'colors':{'shallow':[0.1,0.8,0.7,1]}}}", path);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, req);
	g_assert_cmpstr (json_object_get_string_member (result_object (r), "shape"), ==, "lake");

	/* the preview and grids exist; save writes the engine text */
	g_assert_nonnull (lrg_editor_water_editor_get_preview (we));
	g_assert_nonnull (lrg_editor_water_editor_get_grid (we));
	g_free (req);
	req = g_strdup_printf ("{'id':1,'method':'water.save','params':{'path':'%s'}}", path);
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, req);
	g_assert_false (lrg_editor_data_editor_get_dirty (ed));
	{
		g_autoptr(LrgWaterBody) loaded = lrg_water_body_new_from_file (path, &error);

		g_assert_no_error (error);
		g_assert_cmpuint (lrg_water_body_get_wave_count (loaded), ==, 3);
		g_assert_cmpint (lrg_water_body_get_shape (loaded), ==, LRG_WATER_BODY_LAKE);
	}

	/* the viewport handles of a node on this file push into the editor */
	{
		LrgNode *lake = lrg_editor_find_node (f->editor, "Lake");
		guint    n = lrg_water_body_get_n_points (lrg_editor_water_editor_get_body (we));

		g_free (req);
		req = g_strdup_printf ("{'id':1,'method':'water.add_point','params':{'node':'%s','x':3,'z':3}}",
		                       lrg_node_get_guid (lake));
		g_clear_pointer (&r, json_node_unref);
		r = efx_ipc_result (f, req);
		g_assert_cmpuint (lrg_water_body_get_n_points (lrg_editor_water_editor_get_body (we)), ==, n + 1);
	}
}

/* ==========================================================================
 * GPU: the scene renderer draws terrain and water nodes
 * ========================================================================== */

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-editor-app-terrain");
	return IsWindowReady ();
}

static void
test_gpu_render (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorSceneRenderer *renderer;
	LrgEditorTerrainPanel  *panel;
	LrgNode                *node;
	g_autofree gchar       *shot = NULL;
	g_autofree gchar       *req = NULL;
	g_autofree gchar       *reply = NULL;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}
	node = terrain_level (f);
	panel = lrg_editor_terrain_panel_find (f->app);
	renderer = lrg_editor_app_get_scene_renderer (f->app);
	efx_ipc_result (f, "{'id':1,'method':'terrain.stroke','params':{'tool':'raise','points':[[0,0]],"
	                   "'strength':10,'radius':20}}");
	efx_ipc_result (f, "{'id':1,'method':'terrain.create','params':{'kind':'water','name':'Pond'}}");
	lrg_editor_select (f->editor, node, FALSE);
	lrg_editor_terrain_panel_set_tool (panel, LRG_EDITOR_TERRAIN_TOOL_RAISE);
	camera_above (f, 90.0f);
	efx_frames (f, 6);
	g_assert_cmpuint (lrg_editor_scene_renderer_get_drawn (renderer), >, 1);
	shot = g_build_filename (f->dir, "terrain.png", NULL);
	req = g_strdup_printf ("{'id':1,'method':'view.screenshot','params':{'path':'%s'}}", shot);
	reply = efx_ipc (f, req);
	if (strstr (reply, "\"error\"") == NULL)
		g_assert_true (g_file_test (shot, G_FILE_TEST_EXISTS));
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	gpu_ready = gpu_init ();
	g_test_add ("/editor-app/terrain/create", EditorAppFixture, NULL, efx_setup, test_create, efx_teardown);
	g_test_add ("/editor-app/terrain/stroke", EditorAppFixture, NULL, efx_setup, test_stroke, efx_teardown);
	g_test_add ("/editor-app/terrain/layers", EditorAppFixture, NULL, efx_setup, test_layers, efx_teardown);
	g_test_add ("/editor-app/terrain/panel", EditorAppFixture, NULL, efx_setup, test_panel, efx_teardown);
	g_test_add ("/editor-app/terrain/viewport-brush", EditorAppFixture, NULL, efx_setup, test_viewport_brush,
	            efx_teardown);
	g_test_add ("/editor-app/terrain/water-node", EditorAppFixture, NULL, efx_setup, test_water_node, efx_teardown);
	g_test_add ("/editor-app/terrain/water-editor", EditorAppFixture, NULL, efx_setup, test_water_editor,
	            efx_teardown);
	g_test_add ("/editor-app/terrain/gpu-render", EditorAppFixture, NULL, efx_setup, test_gpu_render, efx_teardown);

	result = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return result;
}
