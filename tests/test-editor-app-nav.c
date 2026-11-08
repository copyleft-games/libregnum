/* test-editor-app-nav.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Navigation in the editor (0.9.0 M6/M7): the Navigation panel (the
 * level's navigation: block, the LrgNavBake form, areas with boxes,
 * off-mesh links, one undo step per edit), bake (sync, async, cancel),
 * save and the level round trip, the viewport overlay recording, the
 * area-box and link handles driven with a synthetic view, play mode
 * (the crowd's agents, the debug agent), the nav.* IPC methods and the
 * hierarchy badge.  Every test level is built in the test.  The GPU case
 * (the overlay inside a real render) runs under the isolated Xvfb wrapper
 * (build-aux/xrun.sh) and skips elsewhere.
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
static LrgEditorNavPanel *
nav_panel (EditorAppFixture *f)
{
	LrgEditorNavPanel *panel = lrg_editor_nav_panel_find (f->app);

	if (panel == NULL)
	{
		LrgDockPanel *dock = lrg_dock_panel_new (LRG_EDITOR_NAV_PANEL_ID, "Navigation");

		panel = lrg_editor_nav_panel_new (f->app);
		lrg_dock_panel_set_child (dock, LRG_WIDGET (panel));
		g_object_unref (panel);
		lrg_editor_app_add_panel (f->app, dock, "tab:inspector");
	}
	efx_frames (f, 2);
	return panel;
}

/* Brings the navigation tab to the front and lays the window out. */
static void
present (EditorAppFixture *f)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *p = lrg_dock_area_get_panel (dock, LRG_EDITOR_NAV_PANEL_ID);

	if (p != NULL)
		lrg_dock_area_present_panel (dock, p);
	efx_frames (f, 3);
}

static LrgNode *
create_node (EditorAppFixture *f,
             const gchar      *json_params)
{
	g_autofree gchar   *req = g_strdup_printf ("{'id':1,'method':'node.create','params':%s}", json_params);
	g_autoptr(JsonNode) r = efx_ipc_result (f, req);

	return lrg_editor_find_node (f->editor, json_object_get_string_member (json_node_get_object (r), "guid"));
}

/* A 20 x 20 m floor (a scaled cube, so it is a solid slab) with a 2 x 2 m
 * pillar in the middle. */
static void
floor_level (EditorAppFixture *f)
{
	LrgNode *ground, *pillar;

	lrg_editor_new_level (f->editor, "nav");
	ground = create_node (f, "{'name':'Ground','kind':'primitive','primitive':'cube'}");
	pillar = create_node (f, "{'name':'Pillar','kind':'primitive','primitive':'cube'}");
	g_assert_nonnull (ground);
	g_assert_nonnull (pillar);
	lrg_node_set_location_xyz (ground, 0.0f, -0.5f, 0.0f);
	lrg_node_set_scale_xyz (ground, 20.0f, 1.0f, 20.0f);
	lrg_node_set_location_xyz (pillar, 0.0f, 1.0f, 0.0f);
	lrg_node_set_scale_xyz (pillar, 2.0f, 2.0f, 2.0f);
	lrg_editor_clear_history (f->editor);
}

/* Saves the current level as @name in the fixture directory. */
static gchar *
save_level (EditorAppFixture *f,
            const gchar      *name)
{
	g_autoptr(GError) error = NULL;
	gchar            *path = g_build_filename (f->dir, name, NULL);

	g_assert_true (lrg_editor_save_level (f->editor, path, &error));
	g_assert_no_error (error);
	return path;
}

static guint
undo_count (EditorAppFixture *f)
{
	return lrg_editor_get_undo_count (f->editor);
}

static gfloat
agent_radius (LrgEditorNavPanel *panel)
{
	gfloat r, h, s, st;

	lrg_nav_bake_get_agent (lrg_editor_nav_panel_get_settings (panel), &r, &h, &s, &st);
	return r;
}

static void
gv_double (GValue  *v,
           gdouble  d)
{
	g_value_init (v, G_TYPE_DOUBLE);
	g_value_set_double (v, d);
}

static void
gv_string (GValue      *v,
           const gchar *s)
{
	g_value_init (v, G_TYPE_STRING);
	g_value_set_string (v, s);
}

static JsonObject *
res_obj (JsonNode *n)
{
	return json_node_get_object (n);
}

/* A perspective view looking down at the origin from above and behind. */
static void
top_view (LrgEditorView *view)
{
	lrg_editor_view_init_default (view, 800.0f, 600.0f);
	view->position[0] = 0.0f;
	view->position[1] = 18.0f;
	view->position[2] = 12.0f;
	view->target[0] = 0.0f;
	view->target[1] = 0.0f;
	view->target[2] = 0.0f;
}

/* ==========================================================================
 * Settings, undo, the form
 * ========================================================================== */

static void
test_settings (EditorAppFixture *f,
               gconstpointer     data)
{
	LrgEditorNavPanel *panel = nav_panel (f);
	g_autoptr(GError)  error = NULL;
	g_auto(GValue)     v = G_VALUE_INIT;
	g_auto(GValue)     tags = G_VALUE_INIT;
	g_auto(GValue)     bmin = G_VALUE_INIT;
	const gchar *const *inc;
	gfloat             mn[3], mx[3];
	guint              before;

	floor_level (f);
	present (f);
	g_assert_nonnull (lrg_editor_nav_panel_get_settings (panel));
	g_assert_null (lrg_editor_nav_panel_get_settings_path (panel));
	g_assert_false (lrg_editor_nav_panel_get_dirty (panel));
	g_assert_true (lrg_widget_get_visible (efx_find (f, "nav-scroll")));
	g_assert_nonnull (lrg_property_grid_get_row (lrg_editor_nav_panel_get_grid (panel), "agent-radius"));

	/* one field, one undo step */
	before = undo_count (f);
	gv_double (&v, 0.55);
	g_assert_true (lrg_editor_nav_panel_set_field (panel, "agent_radius", &v, &error));
	g_assert_no_error (error);
	g_assert_cmpfloat_with_epsilon (agent_radius (panel), 0.55f, 1e-5f);
	g_assert_cmpuint (undo_count (f), ==, before + 1);
	g_assert_true (lrg_editor_nav_panel_get_dirty (panel));
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat_with_epsilon (agent_radius (panel), 0.3f, 1e-5f);
	g_assert_false (lrg_editor_nav_panel_get_dirty (panel));
	lrg_editor_redo (f->editor);
	g_assert_cmpfloat_with_epsilon (agent_radius (panel), 0.55f, 1e-5f);

	/* clamped to the field's range */
	g_value_unset (&v);
	gv_double (&v, 120.0);
	g_assert_true (lrg_editor_nav_panel_set_field (panel, "max-slope", &v, &error));
	{
		gfloat r, h, s, st;

		lrg_nav_bake_get_agent (lrg_editor_nav_panel_get_settings (panel), &r, &h, &s, &st);
		g_assert_cmpfloat (s, <=, 89.0f);
	}

	/* tag lists from a comma separated string */
	gv_string (&tags, "floor, stairs");
	g_assert_true (lrg_editor_nav_panel_set_field (panel, "include-tags", &tags, &error));
	inc = lrg_nav_bake_get_include_tags (lrg_editor_nav_panel_get_settings (panel));
	g_assert_nonnull (inc);
	g_assert_cmpstr (inc[0], ==, "floor");
	g_assert_cmpstr (inc[1], ==, "stairs");

	/* bounds: remembered while off, applied when switched on */
	gv_string (&bmin, "-5, -1, -5");
	g_assert_true (lrg_editor_nav_panel_set_field (panel, "bounds-min", &bmin, &error));
	g_assert_false (lrg_nav_bake_get_bounds (lrg_editor_nav_panel_get_settings (panel), mn, mx));
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, TRUE);
	g_assert_true (lrg_editor_nav_panel_set_field (panel, "use-bounds", &v, &error));
	g_assert_true (lrg_nav_bake_get_bounds (lrg_editor_nav_panel_get_settings (panel), mn, mx));
	g_assert_cmpfloat_with_epsilon (mn[0], -5.0f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (mx[0], 50.0f, 1e-5f);

	/* the level's block */
	g_value_unset (&v);
	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, TRUE);
	g_assert_true (lrg_editor_nav_panel_set_field (panel, "bake_on_load", &v, &error));
	g_assert_true (lrg_level_get_nav_bake_on_load (lrg_editor_get_level (f->editor)));
	lrg_editor_undo (f->editor);
	g_assert_false (lrg_level_get_nav_bake_on_load (lrg_editor_get_level (f->editor)));

	/* unknown keys fail without a step */
	before = undo_count (f);
	g_assert_false (lrg_editor_nav_panel_set_field (panel, "no-such-field", &v, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_clear_error (&error);
	g_assert_cmpuint (undo_count (f), ==, before);

	/* the form (what the grid edits) commits through the same path */
	before = undo_count (f);
	g_object_set (G_OBJECT (lrg_object_property_source_get_object (LRG_OBJECT_PROPERTY_SOURCE (
	                  lrg_property_grid_get_source (lrg_editor_nav_panel_get_grid (panel), 0)))),
	              "cell-size", 0.4f, NULL);
	g_assert_cmpfloat_with_epsilon (lrg_nav_bake_get_cell_size (lrg_editor_nav_panel_get_settings (panel)), 0.4f, 1e-5f);
	g_assert_cmpuint (undo_count (f), ==, before + 1);
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat_with_epsilon (lrg_nav_bake_get_cell_size (lrg_editor_nav_panel_get_settings (panel)), 0.25f, 1e-5f);
	efx_frames (f, 2);
}

/* ==========================================================================
 * Areas, boxes, links
 * ========================================================================== */

static void
test_areas_links (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorNavPanel *panel = nav_panel (f);
	LrgNavBake        *bake;
	g_autoptr(GError)  error = NULL;
	const gfloat       mn[3] = { 4.0f, -1.0f, -2.0f };
	const gfloat       mx[3] = { 12.0f, 1.0f, 2.0f };
	const gfloat       s[3] = { -4.0f, 0.0f, 0.0f };
	const gfloat       e[3] = { -6.0f, 2.0f, 0.0f };
	gfloat             bm[3], bx[3];
	const gchar       *name = NULL;
	const gchar       *area = NULL;
	guint32            tag = 0;
	gint               a, b, l, sa, sb, sl;
	guint              before;

	floor_level (f);
	present (f);

	/* add: auto tag, selected */
	a = lrg_editor_nav_panel_add_area (panel, "mud", 0, 3.0f, &error);
	g_assert_no_error (error);
	g_assert_cmpint (a, ==, 0);
	bake = lrg_editor_nav_panel_get_settings (panel);
	lrg_nav_bake_get_area (bake, 0, &name, &tag, NULL);
	g_assert_cmpstr (name, ==, "mud");
	g_assert_cmpuint (tag, ==, 1);
	g_assert_cmpint (lrg_editor_nav_panel_add_area (panel, "gate", 7, 1.0f, &error), ==, 1);

	/* a name or tag taken twice fails */
	g_assert_cmpint (lrg_editor_nav_panel_add_area (panel, "mud", 0, 1.0f, &error), ==, -1);
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
	g_clear_error (&error);

	/* boxes */
	b = lrg_editor_nav_panel_add_area_box (panel, 0, mx, mn, &error);   /* corners swapped: sorted */
	g_assert_cmpint (b, ==, 0);
	bake = lrg_editor_nav_panel_get_settings (panel);
	g_assert_true (lrg_nav_bake_get_area_box (bake, 0, 0, bm, bx));
	g_assert_cmpfloat (bm[0], ==, 4.0f);
	g_assert_cmpfloat (bx[0], ==, 12.0f);
	lrg_editor_nav_panel_get_selection (panel, &sa, &sb, &sl);
	g_assert_cmpint (sa, ==, 0);
	g_assert_cmpint (sb, ==, 0);

	/* a link through the gate */
	l = lrg_editor_nav_panel_add_link (panel, s, e, FALSE, 2.0f, "gate", &error);
	g_assert_no_error (error);
	g_assert_cmpint (l, ==, 0);
	g_assert_cmpint (lrg_editor_nav_panel_add_link (panel, s, e, TRUE, 0.0f, "nowhere", &error), ==, -1);
	g_clear_error (&error);

	/* renaming the area renames the link's area */
	before = undo_count (f);
	g_assert_true (lrg_editor_nav_panel_set_area (panel, 1, "door", 0, 0.0f, &error));
	g_assert_no_error (error);
	g_assert_cmpuint (undo_count (f), ==, before + 1);
	bake = lrg_editor_nav_panel_get_settings (panel);
	lrg_nav_bake_get_link (bake, 0, NULL, NULL, NULL, NULL, &area);
	g_assert_cmpstr (area, ==, "door");
	lrg_editor_undo (f->editor);
	bake = lrg_editor_nav_panel_get_settings (panel);
	lrg_nav_bake_get_link (bake, 0, NULL, NULL, NULL, NULL, &area);
	g_assert_cmpstr (area, ==, "gate");

	/* set a box, set a link (keeps its index) */
	{
		const gfloat m2[3] = { 0.0f, -1.0f, 0.0f };
		const gfloat x2[3] = { 2.0f, 1.0f, 2.0f };
		const gfloat e2[3] = { -8.0f, 3.0f, 1.0f };
		gfloat       got[3];
		gboolean     bidir = FALSE;

		g_assert_true (lrg_editor_nav_panel_set_area_box (panel, 0, 0, m2, x2, &error));
		g_assert_true (lrg_nav_bake_get_area_box (lrg_editor_nav_panel_get_settings (panel), 0, 0, bm, bx));
		g_assert_cmpfloat (bx[2], ==, 2.0f);
		g_assert_true (lrg_editor_nav_panel_set_link (panel, 0, s, e2, TRUE, 0.5f, NULL, &error));
		g_assert_true (lrg_nav_bake_get_link (lrg_editor_nav_panel_get_settings (panel), 0, NULL, got, &bidir, NULL,
		                                      &area));
		g_assert_cmpfloat (got[0], ==, -8.0f);
		g_assert_true (bidir);
		g_assert_null (area);
	}

	/* remove, and undo the removal */
	bake = lrg_editor_nav_panel_get_settings (panel);
	g_assert_true (lrg_editor_nav_panel_remove_area (panel, 0));
	g_assert_cmpuint (lrg_nav_bake_get_n_areas (lrg_editor_nav_panel_get_settings (panel)), ==, 1);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_nav_bake_get_n_areas (lrg_editor_nav_panel_get_settings (panel)), ==, 2);
	g_assert_cmpuint (lrg_nav_bake_get_n_area_boxes (lrg_editor_nav_panel_get_settings (panel), 0), ==, 1);
	g_assert_true (lrg_editor_nav_panel_remove_area_box (panel, 0, 0));
	g_assert_true (lrg_editor_nav_panel_remove_link (panel, 0));
	g_assert_false (lrg_editor_nav_panel_remove_link (panel, 0));

	/* the lists and the selected-area form are shown */
	lrg_editor_nav_panel_select (panel, 0, -1, -1);
	efx_frames (f, 2);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "nav-area-grid")));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "nav-link-grid")));
	g_assert_cmpfloat (lrg_widget_get_height (efx_find (f, "nav-area-list")), >=, 2.0f * 24.0f);

	/* a click on the first area row's "add box" button adds a box */
	{
		LrgWidget *list = efx_find (f, "nav-area-list");
		gfloat     x, y, w, h;
		guint      n0 = lrg_nav_bake_get_n_area_boxes (lrg_editor_nav_panel_get_settings (panel), 0);

		efx_rect_of (list, &x, &y, &w, &h);
		if (w > 60.0f && y > 0.0f && y + 12.0f < EFX_H)
		{
			efx_click (f, x + w - 22.0f * 2.0f - 2.0f + 11.0f, y + 1.0f + 12.0f);
			g_assert_cmpuint (lrg_nav_bake_get_n_area_boxes (lrg_editor_nav_panel_get_settings (panel), 0), ==, n0 + 1);
		}
	}
}

/* ==========================================================================
 * Bake, overlay, save, round trip
 * ========================================================================== */

static void
test_bake (EditorAppFixture *f,
           gconstpointer     data)
{
	LrgEditorNavPanel     *panel = nav_panel (f);
	g_autoptr(GError)      error = NULL;
	g_autoptr(LrgDebugDraw) draw = lrg_debug_draw_new ();
	LrgNavPolyMesh        *mesh;
	guint                  with_mesh;

	floor_level (f);
	lrg_editor_nav_panel_set_auto_save (panel, FALSE);
	g_assert_true (lrg_editor_nav_panel_bake (panel, &error));
	g_assert_no_error (error);
	mesh = lrg_editor_nav_panel_get_mesh (panel);
	g_assert_nonnull (mesh);
	g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_count (mesh), >, 0);
	g_assert_cmpuint (lrg_editor_nav_panel_get_last_triangle_count (panel), >, 0);
	g_assert_cmpfloat (lrg_editor_nav_panel_get_last_bake_seconds (panel), >, 0.0);
	g_assert_true (lrg_editor_nav_panel_has_unsaved (panel));

	/* the pillar is a hole: nothing walkable at its centre */
	{
		const gfloat centre[3] = { 0.0f, 0.0f, 0.0f };
		const gfloat corner[3] = { 8.0f, 0.0f, 8.0f };
		const gfloat ext[3] = { 0.2f, 0.5f, 0.2f };

		g_assert_false (lrg_nav_poly_mesh_find_nearest (mesh, centre, ext, NULL, NULL));
		g_assert_true (lrg_nav_poly_mesh_find_nearest (mesh, corner, ext, NULL, NULL));
	}

	/* the overlay (on after a bake) records the mesh and the settings */
	g_assert_cmpuint (lrg_editor_nav_panel_get_overlay (panel), !=, LRG_NAV_DEBUG_NONE);
	with_mesh = lrg_editor_nav_panel_record_overlay (panel, draw);
	g_assert_cmpuint (with_mesh, >, 0);
	{
		const gfloat mn[3] = { 4.0f, -1.0f, -2.0f };
		const gfloat mx[3] = { 8.0f, 1.0f, 2.0f };
		gint         a = lrg_editor_nav_panel_add_area (panel, "mud", 0, 3.0f, NULL);

		lrg_editor_nav_panel_add_area_box (panel, (guint) a, mn, mx, NULL);
		g_assert_cmpuint (lrg_editor_nav_panel_record_overlay (panel, draw), >, with_mesh);
	}
	lrg_editor_nav_panel_set_overlay (panel, LRG_NAV_DEBUG_NONE);
	g_assert_cmpuint (lrg_editor_nav_panel_record_overlay (panel, draw), ==, 0);

	/* a rebake with the area: its polygons carry the tag */
	g_assert_true (lrg_editor_nav_panel_bake (panel, &error));
	{
		const gfloat in_mud[3] = { 6.0f, 0.0f, 0.0f };
		const gfloat ext[3] = { 0.2f, 0.5f, 0.2f };
		guint        poly = 0;
		guint32      tag = 0;

		g_assert_true (lrg_nav_poly_mesh_find_nearest (lrg_editor_nav_panel_get_mesh (panel), in_mud, ext, NULL, &poly));
		lrg_nav_poly_mesh_get_polygon (lrg_editor_nav_panel_get_mesh (panel), poly, NULL, NULL, &tag, NULL);
		g_assert_cmpuint (tag, ==, 1);
	}

	/* clear: the mesh goes; undo brings back only the reference */
	g_assert_true (lrg_editor_nav_panel_clear (panel));
	g_assert_null (lrg_editor_nav_panel_get_mesh (panel));
	g_assert_false (lrg_editor_nav_panel_has_unsaved (panel));

	/* nothing to bake */
	lrg_editor_new_level (f->editor, "empty");
	efx_frames (f, 1);
	g_assert_false (lrg_editor_nav_panel_bake (panel, &error));
	g_assert_nonnull (error);
	g_assert_nonnull (lrg_editor_nav_panel_get_error (panel));
	g_clear_error (&error);
	efx_frames (f, 2);
}

static void
test_async (EditorAppFixture *f,
            gconstpointer     data)
{
	LrgEditorNavPanel *panel = nav_panel (f);
	g_autoptr(GError)  error = NULL;
	gint               i;

	floor_level (f);
	lrg_editor_nav_panel_set_auto_save (panel, FALSE);

	/* a cancelled bake leaves nothing */
	g_assert_true (lrg_editor_nav_panel_bake_async (panel, &error));
	g_assert_true (lrg_editor_nav_panel_get_baking (panel));
	g_assert_false (lrg_editor_nav_panel_bake_async (panel, &error));
	g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
	g_clear_error (&error);
	g_assert_true (lrg_editor_nav_panel_cancel (panel));
	g_assert_false (lrg_editor_nav_panel_get_baking (panel));
	for (i = 0; i < 200; i++)
	{
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	g_assert_null (lrg_editor_nav_panel_get_mesh (panel));

	/* a finished one arrives on the main context */
	present (f);
	g_assert_true (lrg_editor_nav_panel_bake_async (panel, &error));
	/* the bar shows at once (the worker may finish within a frame) */
	g_assert_true (lrg_widget_get_visible (efx_find (f, "nav-progress")));
	for (i = 0; i < 2000 && lrg_editor_nav_panel_get_baking (panel); i++)
	{
		g_main_context_iteration (NULL, FALSE);
		g_usleep (2000);
	}
	g_assert_false (lrg_editor_nav_panel_get_baking (panel));
	g_assert_nonnull (lrg_editor_nav_panel_get_mesh (panel));
	g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_count (lrg_editor_nav_panel_get_mesh (panel)), >, 0);
	efx_frames (f, 2);
}

static void
test_save_round_trip (EditorAppFixture *f,
                      gconstpointer     data)
{
	LrgEditorNavPanel *panel = nav_panel (f);
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *level = NULL;
	g_autofree gchar  *rnav = NULL;
	g_autofree gchar  *mesh_path = NULL;
	g_autofree gchar  *text = NULL;
	g_auto(GValue)     v = G_VALUE_INIT;
	guint              polys;

	floor_level (f);

	/* saving needs a level file */
	g_assert_false (lrg_editor_nav_panel_save (panel, &error));
	g_clear_error (&error);
	level = save_level (f, "yard.rlevel");

	/* a bake of a saved level is saved at once (auto save) */
	lrg_editor_nav_panel_add_area (panel, "mud", 2, 3.0f, NULL);
	g_assert_true (lrg_editor_nav_panel_bake (panel, &error));
	g_assert_no_error (error);
	g_assert_false (lrg_editor_nav_panel_has_unsaved (panel));
	polys = lrg_nav_poly_mesh_get_polygon_count (lrg_editor_nav_panel_get_mesh (panel));
	rnav = g_build_filename (f->dir, "yard.rnav", NULL);
	mesh_path = g_build_filename (f->dir, "yard.rnavmesh", NULL);
	g_assert_true (g_file_test (rnav, G_FILE_TEST_EXISTS));
	g_assert_true (g_file_test (mesh_path, G_FILE_TEST_EXISTS));
	g_assert_cmpstr (lrg_level_get_nav_settings_ref (lrg_editor_get_level (f->editor)), ==, "yard.rnav");
	g_assert_cmpstr (lrg_level_get_nav_data_ref (lrg_editor_get_level (f->editor)), ==, "yard.rnavmesh");
	g_assert_cmpstr (lrg_editor_nav_panel_get_settings_path (panel), ==, rnav);
	g_assert_false (lrg_editor_nav_panel_get_dirty (panel));

	/* an edit is dirty; saving the level writes the .rnav too */
	gv_double (&v, 0.45);
	g_assert_true (lrg_editor_nav_panel_set_field (panel, "agent-radius", &v, &error));
	g_assert_true (lrg_editor_nav_panel_get_dirty (panel));
	g_free (save_level (f, "yard.rlevel"));
	g_assert_false (lrg_editor_nav_panel_get_dirty (panel));
	g_assert_true (g_file_get_contents (rnav, &text, NULL, NULL));
	g_assert_nonnull (strstr (text, "radius: 0.45"));
	g_assert_nonnull (strstr (text, "name: mud"));

	/* reload: the panel follows the level's files */
	g_assert_true (lrg_editor_load_level (f->editor, level, &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	g_assert_cmpstr (lrg_editor_nav_panel_get_settings_path (panel), ==, rnav);
	g_assert_cmpfloat_with_epsilon (agent_radius (panel), 0.45f, 1e-5f);
	g_assert_cmpuint (lrg_nav_bake_get_n_areas (lrg_editor_nav_panel_get_settings (panel)), ==, 1);
	g_assert_nonnull (lrg_editor_nav_panel_get_mesh (panel));
	g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_count (lrg_editor_nav_panel_get_mesh (panel)), ==, polys);
	g_assert_false (lrg_editor_nav_panel_get_dirty (panel));

	/* New writes a fresh .rnav and points the level at it (undoable) */
	{
		g_autofree gchar *other = g_build_filename (f->dir, "nav", "alt.rnav", NULL);

		g_assert_true (lrg_editor_nav_panel_new_settings (panel, "nav/alt.rnav", &error));
		g_assert_no_error (error);
		g_assert_true (g_file_test (other, G_FILE_TEST_EXISTS));
		g_assert_cmpstr (lrg_level_get_nav_settings_ref (lrg_editor_get_level (f->editor)), ==, "nav/alt.rnav");
		g_assert_cmpfloat_with_epsilon (agent_radius (panel), 0.3f, 1e-5f);
		lrg_editor_undo (f->editor);
		g_assert_cmpstr (lrg_level_get_nav_settings_ref (lrg_editor_get_level (f->editor)), ==, "yard.rnav");
		g_assert_cmpfloat_with_epsilon (agent_radius (panel), 0.45f, 1e-5f);

		/* and Open loads an existing one */
		g_assert_true (lrg_editor_nav_panel_open_settings (panel, other, &error));
		g_assert_cmpstr (lrg_level_get_nav_settings_ref (lrg_editor_get_level (f->editor)), ==, "nav/alt.rnav");
		g_assert_false (lrg_editor_nav_panel_open_settings (panel, "missing.rnav", &error));
		g_clear_error (&error);
	}

	/* the level instantiates with the saved navigation */
	{
		g_autoptr(LrgWorld3D) world = lrg_world3d_new ();
		g_autoptr(LrgLevel)   loaded = NULL;

		loaded = lrg_level_load_world3d (level, world, &error);
		g_assert_no_error (error);
		g_assert_nonnull (lrg_world3d_get_navigation (world));
		g_assert_cmpuint (lrg_nav_poly_mesh_get_polygon_count (lrg_world3d_get_navigation (world)), ==, polys);
	}
}

/* ==========================================================================
 * Handles
 * ========================================================================== */

static void
test_handles (EditorAppFixture *f,
              gconstpointer     data)
{
	LrgEditorNavPanel *panel = nav_panel (f);
	LrgEditorView      view;
	const gfloat       mn[3] = { 2.0f, -1.0f, 2.0f };
	const gfloat       mx[3] = { 4.0f, 1.0f, 4.0f };
	const gfloat       s[3] = { -3.0f, 0.0f, 0.0f };
	const gfloat       e[3] = { -5.0f, 0.0f, 3.0f };
	gfloat             p[3], u, v, target[3], bm[3], bx[3];
	guint              before;
	gint               a;

	floor_level (f);
	top_view (&view);
	g_assert_false (lrg_editor_nav_panel_get_handle_position (panel, LRG_EDITOR_NAV_HANDLE_BOX_MAX, p));

	a = lrg_editor_nav_panel_add_area (panel, "mud", 0, 3.0f, NULL);
	g_assert_cmpint (lrg_editor_nav_panel_add_area_box (panel, (guint) a, mn, mx, NULL), ==, 0);
	g_assert_true (lrg_editor_nav_panel_get_handle_position (panel, LRG_EDITOR_NAV_HANDLE_BOX_MAX, p));
	g_assert_cmpfloat (p[0], ==, 4.0f);
	g_assert_cmpfloat (p[1], ==, 0.0f);
	g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
	g_assert_cmpint (lrg_editor_nav_panel_hit_test (panel, &view, u, v), ==,
	                 LRG_EDITOR_NAV_HANDLE_BOX_MAX);

	/* drag the max corner to (6, 0, 7): one undo step */
	before = undo_count (f);
	target[0] = 6.0f;
	target[1] = 0.0f;
	target[2] = 7.0f;
	{
		gfloat tu, tv;

		g_assert_true (lrg_editor_view_project (&view, target, &tu, &tv, NULL));
		g_assert_true (lrg_editor_nav_panel_handle_press (panel, &view, GRL_MOUSE_BUTTON_LEFT, u, v));
		g_assert_true (lrg_editor_nav_panel_handle_motion (panel, &view, (u + tu) * 0.5f, (v + tv) * 0.5f, FALSE, NULL));
		g_assert_true (lrg_editor_nav_panel_handle_motion (panel, &view, tu, tv, FALSE, NULL));
		g_assert_true (lrg_editor_nav_panel_handle_release (panel, &view, tu, tv, TRUE));
	}
	g_assert_cmpuint (undo_count (f), ==, before + 1);
	g_assert_true (lrg_nav_bake_get_area_box (lrg_editor_nav_panel_get_settings (panel), 0, 0, bm, bx));
	g_assert_cmpfloat_with_epsilon (bx[0], 6.0f, 1e-3f);
	g_assert_cmpfloat_with_epsilon (bx[2], 7.0f, 1e-3f);
	g_assert_cmpfloat (bm[0], ==, 2.0f);
	lrg_editor_undo (f->editor);
	g_assert_true (lrg_nav_bake_get_area_box (lrg_editor_nav_panel_get_settings (panel), 0, 0, bm, bx));
	g_assert_cmpfloat (bx[0], ==, 4.0f);

	/* a right press during a drag cancels it */
	g_assert_true (lrg_editor_nav_panel_get_handle_position (panel, LRG_EDITOR_NAV_HANDLE_BOX_CENTER, p));
	lrg_editor_view_project (&view, p, &u, &v, NULL);
	before = undo_count (f);
	g_assert_true (lrg_editor_nav_panel_handle_press (panel, &view, GRL_MOUSE_BUTTON_LEFT, u, v));
	g_assert_true (lrg_editor_nav_panel_handle_motion (panel, &view, u + 60.0f, v, FALSE, NULL));
	g_assert_true (lrg_nav_bake_get_area_box (lrg_editor_nav_panel_get_settings (panel), 0, 0, bm, bx));
	g_assert_cmpfloat (bm[0], !=, 2.0f);
	g_assert_true (lrg_editor_nav_panel_handle_press (panel, &view, GRL_MOUSE_BUTTON_RIGHT, u + 60.0f, v));
	g_assert_false (lrg_editor_nav_panel_handle_release (panel, &view, u + 60.0f, v, FALSE));
	g_assert_true (lrg_nav_bake_get_area_box (lrg_editor_nav_panel_get_settings (panel), 0, 0, bm, bx));
	g_assert_cmpfloat (bm[0], ==, 2.0f);
	g_assert_cmpuint (undo_count (f), ==, before);

	/* link end: snapped to 0.25 m */
	g_assert_cmpint (lrg_editor_nav_panel_add_link (panel, s, e, TRUE, 0.0f, NULL, NULL), ==, 0);
	g_assert_true (lrg_editor_nav_panel_get_handle_position (panel, LRG_EDITOR_NAV_HANDLE_LINK_END, p));
	lrg_editor_view_project (&view, p, &u, &v, NULL);
	g_assert_cmpint (lrg_editor_nav_panel_hit_test (panel, &view, u, v), ==, LRG_EDITOR_NAV_HANDLE_LINK_END);
	target[0] = -6.1f;
	target[1] = 0.0f;
	target[2] = 4.1f;
	{
		gfloat tu, tv, got[3];

		lrg_editor_view_project (&view, target, &tu, &tv, NULL);
		g_assert_true (lrg_editor_nav_panel_handle_press (panel, &view, GRL_MOUSE_BUTTON_LEFT, u, v));
		g_assert_true (lrg_editor_nav_panel_handle_release (panel, &view, tu, tv, TRUE));
		lrg_nav_bake_get_link (lrg_editor_nav_panel_get_settings (panel), 0, NULL, got, NULL, NULL, NULL);
		g_assert_cmpfloat_with_epsilon (got[0], -6.0f, 1e-4f);
		g_assert_cmpfloat_with_epsilon (got[2], 4.0f, 1e-4f);
	}

	/* hidden handles neither hit nor paint */
	lrg_editor_nav_panel_set_show_handles (panel, FALSE);
	g_assert_cmpint (lrg_editor_nav_panel_hit_test (panel, &view, u, v), ==, LRG_EDITOR_NAV_HANDLE_NONE);
	g_assert_false (lrg_editor_nav_panel_handle_press (panel, &view, GRL_MOUSE_BUTTON_LEFT, u, v));
	lrg_editor_nav_panel_set_show_handles (panel, TRUE);

	/* paint: the box (3 handles) and the link (2) */
	{
		g_autoptr(LrgPaintList) list = lrg_paint_list_new ();
		LrgTheme               *theme = lrg_widget_get_theme (LRG_WIDGET (panel));

		lrg_editor_nav_panel_select (panel, 0, 0, 0);
		g_assert_cmpuint (lrg_editor_nav_panel_paint_handles (panel, list, theme, &view, 0.0f, 0.0f, 1.0f), ==, 5);
	}
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	LrgEditorNavPanel *panel = nav_panel (f);
	g_autofree gchar  *reply = NULL;
	guint              before;

	/* no level */
	reply = efx_ipc (f, "{'id':1,'method':'nav.set','params':{'key':'cell_size','value':0.3}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);

	floor_level (f);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'nav.get'}");
		JsonObject         *o = res_obj (r);

		g_assert_true (json_node_is_null (json_object_get_member (o, "settings")));
		g_assert_false (json_object_get_boolean_member (o, "dirty"));
		g_assert_cmpfloat_with_epsilon (json_object_get_double_member (json_object_get_object_member (
		                                    json_object_get_object_member (o, "bake"), "agent"), "radius"), 0.3, 1e-5);
		g_assert_false (json_object_get_boolean_member (json_object_get_object_member (o, "mesh"), "present"));
	}

	/* values: one undo step for the batch */
	before = undo_count (f);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'nav.set','params':{'values':{"
		                                           "'agent_radius':0.4,'cell_size':0.2,'exclude_tags':['no-nav'],"
		                                           "'bounds':{'min':[-8,-2,-8],'max':[8,4,8]},'bake_on_load':true}}}");
		JsonObject         *src = json_object_get_object_member (json_object_get_object_member (res_obj (r), "bake"),
		                                                         "sources");

		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (src, "exclude_tags")), ==, 1);
		g_assert_false (json_node_is_null (json_object_get_member (src, "bounds")));
		g_assert_true (json_object_get_boolean_member (res_obj (r), "bake_on_load"));
	}
	g_assert_cmpuint (undo_count (f), ==, before + 1);
	g_assert_cmpfloat_with_epsilon (agent_radius (panel), 0.4f, 1e-5f);

	/* a bad key fails and leaves nothing */
	reply = efx_ipc (f, "{'id':1,'method':'nav.set','params':{'values':{'cell_size':0.3,'bogus':1}}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);
	g_assert_cmpfloat_with_epsilon (lrg_nav_bake_get_cell_size (lrg_editor_nav_panel_get_settings (panel)), 0.2f, 1e-5f);
	g_assert_cmpuint (undo_count (f), ==, before + 1);

	/* areas, boxes, links */
	{
		g_autoptr(JsonNode) a = efx_ipc_result (f, "{'id':1,'method':'nav.area.add','params':{'name':'mud','cost':3}}");
		g_autoptr(JsonNode) b = efx_ipc_result (f, "{'id':1,'method':'nav.area.box.add','params':{'area':'mud',"
		                                           "'min':[3,-1,-2],'max':[6,1,2]}}");
		g_autoptr(JsonNode) l = efx_ipc_result (f, "{'id':1,'method':'nav.link.add','params':{'start':[-3,0,0],"
		                                           "'end':[-5,0,2],'area':'mud','bidirectional':false}}");
		g_autoptr(JsonNode) s = efx_ipc_result (f, "{'id':1,'method':'nav.area.set','params':{'area':0,'name':'swamp',"
		                                           "'cost':4}}");
		g_autoptr(JsonNode) bs = efx_ipc_result (f, "{'id':1,'method':'nav.area.box.set','params':{'area':'swamp',"
		                                            "'box':0,'max':[7,1,3]}}");
		g_autoptr(JsonNode) ls = efx_ipc_result (f, "{'id':1,'method':'nav.link.set','params':{'index':0,'cost':1.5}}");
		g_autoptr(JsonNode) g = efx_ipc_result (f, "{'id':1,'method':'nav.get'}");
		JsonArray          *areas = json_object_get_array_member (res_obj (g), "areas");
		JsonArray          *links = json_object_get_array_member (res_obj (g), "links");
		JsonObject         *area0 = json_array_get_object_element (areas, 0);
		JsonObject         *link0 = json_array_get_object_element (links, 0);

		g_assert_cmpint (json_object_get_int_member (res_obj (a), "index"), ==, 0);
		g_assert_cmpint (json_object_get_int_member (res_obj (b), "index"), ==, 0);
		g_assert_cmpint (json_object_get_int_member (res_obj (l), "index"), ==, 0);
		g_assert_cmpstr (json_object_get_string_member (area0, "name"), ==, "swamp");
		g_assert_cmpfloat (json_object_get_double_member (area0, "cost"), ==, 4.0);
		g_assert_true (g_str_has_prefix (json_object_get_string_member (area0, "color"), "#"));
		g_assert_cmpfloat (json_array_get_double_element (json_object_get_array_member (
		                       json_array_get_object_element (json_object_get_array_member (area0, "boxes"), 0), "max"),
		                       0), ==, 7.0);
		g_assert_cmpstr (json_object_get_string_member (link0, "area"), ==, "swamp");
		g_assert_false (json_object_get_boolean_member (link0, "bidirectional"));
		g_assert_cmpfloat (json_object_get_double_member (link0, "cost"), ==, 1.5);
		(void) s;
		(void) bs;
		(void) ls;
	}
	reply = efx_ipc (f, "{'id':1,'method':'nav.area.remove','params':{'area':'nope'}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);

	/* select, overlay */
	{
		g_autoptr(JsonNode) s = efx_ipc_result (f, "{'id':1,'method':'nav.select','params':{'area':'swamp','link':0}}");
		g_autoptr(JsonNode) o = efx_ipc_result (f, "{'id':1,'method':'nav.overlay','params':{'flags':['polygons',"
		                                           "'areas','handles']}}");
		g_autoptr(JsonNode) o2 = efx_ipc_result (f, "{'id':1,'method':'nav.overlay','params':{'off':['areas'],"
		                                            "'on':['links']}}");
		JsonArray          *flags = json_object_get_array_member (res_obj (o2), "flags");
		guint               i;
		gboolean            links = FALSE, areas = FALSE;

		g_assert_cmpint (json_object_get_int_member (res_obj (s), "area"), ==, 0);
		g_assert_cmpint (json_object_get_int_member (res_obj (s), "box"), ==, 0);
		g_assert_cmpint (json_object_get_int_member (res_obj (s), "link"), ==, 0);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (res_obj (o), "flags")), ==, 3);
		for (i = 0; i < json_array_get_length (flags); i++)
		{
			links = links || g_strcmp0 (json_array_get_string_element (flags, i), "links") == 0;
			areas = areas || g_strcmp0 (json_array_get_string_element (flags, i), "areas") == 0;
		}
		g_assert_true (links);
		g_assert_false (areas);
	}
	reply = efx_ipc (f, "{'id':1,'method':'nav.overlay','params':{'flags':['sparkles']}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);

	/* bake (no save: the level has no file), then clear */
	{
		g_autoptr(JsonNode) b = efx_ipc_result (f, "{'id':1,'method':'nav.bake'}");

		g_assert_cmpint (json_object_get_int_member (res_obj (b), "polygons"), >, 0);
		g_assert_cmpint (json_object_get_int_member (res_obj (b), "triangles"), >, 0);
		g_assert_false (json_object_get_boolean_member (res_obj (b), "saved"));
	}
	reply = efx_ipc (f, "{'id':1,'method':'nav.save'}");
	g_assert_nonnull (strstr (reply, "Save the level first"));
	g_clear_pointer (&reply, g_free);
	{
		g_autoptr(JsonNode) c = efx_ipc_result (f, "{'id':1,'method':'nav.clear'}");

		g_assert_true (json_object_get_boolean_member (res_obj (c), "cleared"));
	}

	/* save a level, then nav.save / nav.new */
	g_free (save_level (f, "court.rlevel"));
	{
		g_autoptr(JsonNode) b = efx_ipc_result (f, "{'id':1,'method':'nav.bake','params':{'save':true}}");
		g_autoptr(JsonNode) g = efx_ipc_result (f, "{'id':1,'method':'nav.get'}");

		g_assert_true (json_object_get_boolean_member (res_obj (b), "saved"));
		g_assert_cmpstr (json_object_get_string_member (res_obj (g), "settings"), ==, "court.rnav");
		g_assert_cmpstr (json_object_get_string_member (res_obj (g), "data"), ==, "court.rnavmesh");
	}
	{
		g_autoptr(JsonNode) n = efx_ipc_result (f, "{'id':1,'method':'nav.new','params':{'path':'other.rnav'}}");

		g_assert_cmpstr (json_object_get_string_member (res_obj (n), "settings"), ==, "other.rnav");
	}

	/* not playing: no agents, debug agent refused */
	{
		g_autoptr(JsonNode) a = efx_ipc_result (f, "{'id':1,'method':'nav.agents'}");

		g_assert_false (json_object_get_boolean_member (res_obj (a), "playing"));
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (res_obj (a), "agents")), ==, 0);
	}
	reply = efx_ipc (f, "{'id':1,'method':'nav.debug_agent','params':{'index':0}}");
	g_assert_nonnull (strstr (reply, "\"error\""));
	g_clear_pointer (&reply, g_free);
	{
		g_autoptr(JsonNode) s = efx_ipc_result (f, "{'id':1,'method':'nav.show'}");

		g_assert_cmpstr (json_object_get_string_member (res_obj (s), "panel"), ==, "navigation");
	}
}

/* ==========================================================================
 * Play mode, hierarchy badge
 * ========================================================================== */

static void
test_play (EditorAppFixture *f,
           gconstpointer     data)
{
	LrgEditorNavPanel          *panel = nav_panel (f);
	g_autoptr(GError)           error = NULL;
	g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new ("nav-agent");
	g_autoptr(LrgDebugDraw)     draw = lrg_debug_draw_new ();
	LrgNode                    *walker;

	floor_level (f);
	walker = create_node (f, "{'name':'Walker','kind':'none'}");
	g_assert_nonnull (walker);
	lrg_node_set_location_xyz (walker, -6.0f, 0.0f, -6.0f);
	lrg_editor_add_component (f->editor, walker, desc);
	lrg_editor_nav_panel_set_auto_save (panel, FALSE);
	g_assert_true (lrg_editor_nav_panel_bake (panel, &error));
	g_assert_null (lrg_editor_nav_panel_get_crowd (panel));

	if (!lrg_editor_app_activate_action (f->app, "play.start") || !lrg_editor_app_is_playing (f->app))
	{
		g_test_skip ("play-in-editor is not available here");
		return;
	}
	efx_frames (f, 3);

	/* the unsaved bake went into the play world */
	if (lrg_editor_nav_panel_get_crowd (panel) == NULL)
	{
		g_test_skip ("the play world is not 3D here");
		lrg_editor_app_activate_action (f->app, "play.stop");
		return;
	}
	{
		LrgEditorPlaySession *play = lrg_editor_app_get_play_session (f->app);
		LrgWorld3D           *world = lrg_editor_play_session_get_world3d (play);
		g_autoptr(GPtrArray)  comps = lrg_world3d_get_components_of_type (world, LRG_TYPE_NAV_AGENT_COMPONENT);
		const gfloat          goal[3] = { 6.0f, 0.0f, 6.0f };

		/* the walker's live component (the only one) */
		g_assert_nonnull (comps);
		g_assert_cmpuint (comps->len, ==, 1);
		g_assert_true (lrg_nav_agent_component_set_destination (g_ptr_array_index (comps, 0), goal));
	}
	efx_frames (f, 20);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'nav.agents'}");
		JsonObject         *o = res_obj (r);
		JsonArray          *agents = json_object_get_array_member (o, "agents");
		JsonObject         *a0;

		g_assert_true (json_object_get_boolean_member (o, "playing"));
		g_assert_cmpuint (json_array_get_length (agents), ==, 1);
		a0 = json_array_get_object_element (agents, 0);
		g_assert_cmpstr (json_object_get_string_member (a0, "entity"), ==, "Walker");
		g_assert_cmpstr (json_object_get_string_member (a0, "guid"), ==, lrg_node_get_guid (walker));
		g_assert_cmpstr (json_object_get_string_member (a0, "state"), ==, "moving");
		g_assert_cmpfloat (json_object_get_double_member (a0, "remaining"), >, 0.0);
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (a0, "path")), >=, 2);
		g_assert_cmpint (json_object_get_int_member (o, "step_count"), >, 0);
	}

	/* the debug agent, by node */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'nav.debug_agent','params':{'node':'Walker'}}");

		g_assert_cmpint (json_object_get_int_member (res_obj (r), "debug"), ==, 0);
		g_assert_cmpint (lrg_editor_nav_panel_get_debug_agent (panel), ==, 0);
	}
	present (f);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "nav-agents-section")));

	/* the overlay records the play world's mesh and crowd */
	g_assert_cmpuint (lrg_editor_nav_panel_record_overlay (panel, draw), >, 0);
	g_assert_true (lrg_editor_nav_panel_set_debug_agent (panel, -1));
	g_assert_cmpint (lrg_editor_nav_panel_get_debug_agent (panel), ==, -1);

	g_assert_true (lrg_editor_app_activate_action (f->app, "play.stop"));
	efx_frames (f, 2);
	g_assert_null (lrg_editor_nav_panel_get_crowd (panel));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "nav-agents-section")));
}

static void
test_hierarchy_badge (EditorAppFixture *f,
                      gconstpointer     data)
{
	g_autoptr(LrgComponentDesc) desc = lrg_component_desc_new ("nav-agent");
	LrgNode                    *walker;
	JsonArray                  *rows;
	guint                       i, found = 0;

	nav_panel (f);
	floor_level (f);
	walker = create_node (f, "{'name':'Walker','kind':'none'}");
	lrg_editor_add_component (f->editor, walker, desc);
	efx_frames (f, 3);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'hierarchy.state'}");

		rows = json_object_get_array_member (json_node_get_object (r), "rows");
		for (i = 0; i < json_array_get_length (rows); i++)
		{
			JsonObject *row = json_array_get_object_element (rows, i);

			if (g_strcmp0 (json_object_get_string_member (row, "name"), "Walker") == 0)
			{
				g_assert_true (json_object_get_boolean_member (row, "nav_agent"));
				found++;
			}
			else if (g_strcmp0 (json_object_get_string_member (row, "name"), "Ground") == 0)
			{
				g_assert_false (json_object_get_boolean_member (row, "nav_agent"));
				found++;
			}
		}
	}
	g_assert_cmpuint (found, ==, 2);
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
}

/* ==========================================================================
 * GPU: the overlay in a real render (xrun.sh)
 * ========================================================================== */

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-editor-app-nav");
	return IsWindowReady ();
}

static void
test_gpu_overlay (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorNavPanel *panel;
	g_autoptr(GError)  error = NULL;
	g_autofree gchar  *shot = NULL;
	g_autofree gchar  *req = NULL;
	g_autofree gchar  *reply = NULL;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}
	panel = nav_panel (f);
	floor_level (f);
	lrg_editor_nav_panel_set_auto_save (panel, FALSE);
	g_assert_true (lrg_editor_nav_panel_bake (panel, &error));
	lrg_editor_nav_panel_set_overlay (panel, LRG_NAV_DEBUG_DEFAULT | LRG_NAV_DEBUG_AREAS);
	efx_frames (f, 5);

	/* a viewport screenshot reads back the render the hook drew into */
	shot = g_build_filename (f->dir, "nav.png", NULL);
	req = g_strdup_printf ("{'id':1,'method':'view.screenshot','params':{'path':'%s'}}", shot);
	reply = efx_ipc (f, req);
	if (strstr (reply, "\"error\"") == NULL)
		g_assert_true (g_file_test (shot, G_FILE_TEST_EXISTS));
	efx_frames (f, 2);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	gpu_ready = gpu_init ();
	/* the registry instantiation and play mode create "nav-agent" from */
	lrg_engine_startup (lrg_engine_get_default (), NULL);
	g_assert_nonnull (lrg_engine_get_registry (lrg_engine_get_default ()));
	g_test_add ("/editor-app/nav/settings", EditorAppFixture, NULL, efx_setup, test_settings, efx_teardown);
	g_test_add ("/editor-app/nav/areas-links", EditorAppFixture, NULL, efx_setup, test_areas_links, efx_teardown);
	g_test_add ("/editor-app/nav/bake", EditorAppFixture, NULL, efx_setup, test_bake, efx_teardown);
	g_test_add ("/editor-app/nav/async", EditorAppFixture, NULL, efx_setup, test_async, efx_teardown);
	g_test_add ("/editor-app/nav/save-round-trip", EditorAppFixture, NULL, efx_setup, test_save_round_trip,
	            efx_teardown);
	g_test_add ("/editor-app/nav/handles", EditorAppFixture, NULL, efx_setup, test_handles, efx_teardown);
	g_test_add ("/editor-app/nav/ipc", EditorAppFixture, NULL, efx_setup, test_ipc, efx_teardown);
	g_test_add ("/editor-app/nav/play", EditorAppFixture, NULL, efx_setup, test_play, efx_teardown);
	g_test_add ("/editor-app/nav/hierarchy-badge", EditorAppFixture, NULL, efx_setup, test_hierarchy_badge,
	            efx_teardown);
	g_test_add ("/editor-app/nav/gpu-overlay", EditorAppFixture, NULL, efx_setup, test_gpu_overlay, efx_teardown);

	result = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return result;
}
