/* test-editor-app-viewport.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The editor's viewport area: LrgEditorViewportSplit layouts, the active
 * viewport, per-viewport cameras, render modes and overlays (through the
 * panel and the view.* IPC methods), asset drops, the camera preview,
 * gizmo drags, render on demand, the light set built from the level, the
 * gizmo painting and the embedded shader sources.
 *
 * Headless by default. The GPU cases (render modes differ, shadows / sky
 * / post produce pixels) run only on an X display without Wayland, i.e.
 * under the isolated Xvfb wrapper (xrun.sh); elsewhere they skip.
 */

#include "editor-app-fixture.h"
#include <raylib.h>
#include <math.h>

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/*
 * The split the window hosts, or (until the shell hosts it) a split put
 * into the "viewport" dock panel in place of the shell's lone panel. The
 * shell's panel is kept alive (the app still ticks it) in @old_panel.
 */
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
		/* The shell hosts the split: lay it out before measuring */
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

/* Panels on screen (in the tree with a size). */
static guint
count_shown (LrgEditorViewportSplit *split)
{
	guint i, n = 0;

	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		LrgWidget *p = LRG_WIDGET (lrg_editor_viewport_split_get_panel (split, i));

		if (lrg_widget_is_ancestor (p, LRG_WIDGET (split)) && lrg_widget_get_width (p) > 1.0f &&
		    lrg_widget_get_height (p) > 1.0f)
			n++;
	}
	return n;
}

/* The centre of a panel's viewport, canvas coordinates. */
static void
viewport_centre (LrgEditorViewportPanel *panel,
                 gfloat                 *x,
                 gfloat                 *y)
{
	gfloat vx, vy, vw, vh;

	efx_rect_of (LRG_WIDGET (lrg_editor_viewport_panel_get_viewport (panel)), &vx, &vy, &vw, &vh);
	g_assert_cmpfloat (vw, >, 10.0f);
	*x = vx + vw * 0.5f;
	*y = vy + vh * 0.5f;
}

static gint64
undo_count (EditorAppFixture *f)
{
	g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'editor.info'}");

	return json_object_get_int_member (json_node_get_object (r), "undo");
}

static LrgNode *
create_node (EditorAppFixture *f,
             const gchar      *json_params)
{
	g_autofree gchar   *req = g_strdup_printf ("{'id':1,'method':'node.create','params':%s}", json_params);
	g_autoptr(JsonNode) r = efx_ipc_result (f, req);
	JsonObject         *o = json_node_get_object (r);

	return lrg_editor_find_node (f->editor, json_object_get_string_member (o, "guid"));
}

/* ==========================================================================
 * Layouts and the active viewport
 * ========================================================================== */

static void
test_layouts (EditorAppFixture *f,
              gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	g_autoptr(JsonNode)     r = NULL;

	g_assert_cmpuint (count_shown (split), ==, 1);
	g_assert_cmpuint (lrg_editor_viewport_split_get_n_visible (split), ==, 1);

	r = efx_ipc_result (f, "{'id':1,'method':'view.layout','params':{'layout':'quad'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "layout"), ==, "quad");
	efx_frames (f, 3);
	g_assert_cmpuint (count_shown (split), ==, 4);

	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'view.layout','params':{'layout':'split-h'}}");
	efx_frames (f, 3);
	g_assert_cmpuint (count_shown (split), ==, 2);
	{
		gfloat x0, y0, w0, h0, x1, y1, w1, h1;

		/* side by side: same top, panel 1 right of panel 0 */
		efx_rect_of (LRG_WIDGET (lrg_editor_viewport_split_get_panel (split, 0)), &x0, &y0, &w0, &h0);
		efx_rect_of (LRG_WIDGET (lrg_editor_viewport_split_get_panel (split, 1)), &x1, &y1, &w1, &h1);
		g_assert_cmpfloat (fabsf (y0 - y1), <, 1.0f);
		g_assert_cmpfloat (x1, >=, x0 + w0 - 1.0f);
	}

	lrg_editor_view_state_set_layout (lrg_editor_app_get_view_state (f->app), LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_V);
	efx_frames (f, 3);
	g_assert_cmpuint (count_shown (split), ==, 2);

	/* Maximise shows one alone without changing the layout */
	lrg_editor_viewport_split_set_maximized (split, 1);
	efx_frames (f, 3);
	g_assert_cmpuint (count_shown (split), ==, 1);
	g_assert_cmpuint (lrg_editor_view_state_get_active_viewport (lrg_editor_app_get_view_state (f->app)), ==, 1);
	g_assert_cmpstr (lrg_editor_view_state_get_layout_name (lrg_editor_app_get_view_state (f->app)), ==, "split-v");
	lrg_editor_viewport_split_set_maximized (split, -1);
	efx_frames (f, 3);
	g_assert_cmpuint (count_shown (split), ==, 2);

	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'view.layout','params':{'layout':'single'}}");
	efx_frames (f, 3);
	g_assert_cmpuint (count_shown (split), ==, 1);

	/* Bad layout names are an error */
	{
		g_autofree gchar *reply = efx_ipc (f, "{'id':1,'method':'view.layout','params':{'layout':'nine'}}");

		g_assert_nonnull (strstr (reply, "\"error\""));
	}
}

static void
test_maximize_button (EditorAppFixture *f,
                      gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);

	lrg_editor_view_state_set_layout (lrg_editor_app_get_view_state (f->app), LRG_EDITOR_VIEWPORT_LAYOUT_QUAD);
	efx_frames (f, 3);
	efx_click_widget (f, efx_find (f, "vp2-maximize"));
	efx_frames (f, 2);
	g_assert_cmpint (lrg_editor_viewport_split_get_maximized (split), ==, 2);
	g_assert_cmpuint (count_shown (split), ==, 1);
	efx_click_widget (f, efx_find (f, "vp2-maximize"));
	efx_frames (f, 2);
	g_assert_cmpint (lrg_editor_viewport_split_get_maximized (split), ==, -1);
	g_assert_cmpuint (count_shown (split), ==, 4);
}

static void
test_active_viewport (EditorAppFixture *f,
                      gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewState     *view = lrg_editor_app_get_view_state (f->app);
	gfloat                  x, y;

	lrg_editor_view_state_set_layout (view, LRG_EDITOR_VIEWPORT_LAYOUT_QUAD);
	efx_frames (f, 3);

	/* A click makes its viewport active */
	viewport_centre (lrg_editor_viewport_split_get_panel (split, 2), &x, &y);
	efx_click (f, x, y);
	g_assert_cmpuint (lrg_editor_view_state_get_active_viewport (view), ==, 2);
	g_assert_true (lrg_editor_viewport_split_get_active_panel (split) == lrg_editor_viewport_split_get_panel (split, 2));

	/* Hovering another (no button held) moves the activity there */
	viewport_centre (lrg_editor_viewport_split_get_panel (split, 1), &x, &y);
	efx_move (f, x, y);
	efx_move (f, x + 3.0f, y + 2.0f);
	g_assert_cmpuint (lrg_editor_view_state_get_active_viewport (view), ==, 1);

	/* The active one gets the accent outline (recorded paint differs) */
	efx_frames (f, 30);
	{
		g_autoptr(LrgPaintList) list = lrg_paint_list_new ();

		lrg_widget_paint (efx_find (f, "viewport-panel-1"), list);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 0);
	}
}

/* ==========================================================================
 * Per-viewport cameras, render modes and overlays
 * ========================================================================== */

static void
test_camera_independence (EditorAppFixture *f,
                          gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	g_autoptr(JsonNode)     before = NULL;
	g_autoptr(JsonNode)     after = NULL;
	g_autoptr(JsonNode)     one = NULL;
	JsonArray              *pos;

	(void) split;
	efx_ipc_result (f, "{'id':1,'method':'view.layout','params':{'layout':'quad'}}");
	efx_frames (f, 2);
	before = efx_ipc_result (f, "{'id':1,'method':'view.get','params':{'viewport':0}}");
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'view.set','params':{'viewport':1,'position':[3,40,7],'target':[3,0,7]}}"));
	after = efx_ipc_result (f, "{'id':1,'method':'view.get','params':{'viewport':0}}");
	one = efx_ipc_result (f, "{'id':1,'method':'view.get','params':{'viewport':1}}");

	/* viewport 0 untouched, viewport 1 moved */
	g_assert_true (json_node_equal (json_object_get_member (json_node_get_object (before), "position"),
	                                json_object_get_member (json_node_get_object (after), "position")));
	pos = json_object_get_array_member (json_node_get_object (one), "position");
	g_assert_cmpfloat (fabs (json_array_get_double_element (pos, 1) - 40.0), <, 1e-4);

	/* The quad defaults: 1..3 are orthographic axis views */
	{
		LrgEditorViewState *view = lrg_editor_app_get_view_state (f->app);

		g_assert_false (lrg_editor_view_state_get_camera (view, 0)->view.orthographic);
		g_assert_cmpint (lrg_editor_view_state_get_camera (view, 2)->axis_view, ==, LRG_EDITOR_AXIS_VIEW_FRONT);
		g_assert_cmpint (lrg_editor_view_state_get_camera (view, 3)->axis_view, ==, LRG_EDITOR_AXIS_VIEW_RIGHT);
	}
}

static void
test_render_mode_overlay_ipc (EditorAppFixture *f,
                              gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewState     *view = lrg_editor_app_get_view_state (f->app);
	g_autoptr(JsonNode)     r = NULL;
	JsonObject             *map;

	efx_ipc_result (f, "{'id':1,'method':'view.layout','params':{'layout':'quad'}}");
	efx_frames (f, 2);

	/* view.render_mode sets one viewport; the header follows */
	r = efx_ipc_result (f, "{'id':1,'method':'view.render_mode','params':{'viewport':2,'mode':'normals'}}");
	g_assert_cmpstr (json_object_get_string_member (json_node_get_object (r), "mode"), ==, "normals");
	g_assert_cmpint (lrg_editor_view_state_get_render_mode (view, 2), ==, LRG_EDITOR_RENDER_MODE_NORMALS);
	g_assert_cmpint (lrg_editor_view_state_get_render_mode (view, 0), ==, LRG_EDITOR_RENDER_MODE_LIT);
	efx_frames (f, 1);
	g_assert_cmpstr (lrg_combo_box_get_selected_text (LRG_COMBO_BOX (efx_find (f, "vp2-render-mode"))), ==, "Normals");
	{
		g_autofree gchar *reply = efx_ipc (f, "{'id':1,'method':'view.render_mode','params':{'mode':'xray'}}");

		g_assert_nonnull (strstr (reply, "\"error\""));
	}

	/* view.overlay: explicit enable/disable, toggle, and the full map */
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'view.overlay','params':{'viewport':1,'name':'shadows','enabled':false}}");
	map = json_object_get_object_member (json_node_get_object (r), "overlays");
	g_assert_false (json_object_get_boolean_member (map, "shadows"));
	g_assert_true (json_object_get_boolean_member (map, "grid"));
	g_assert_false (lrg_editor_view_state_has_overlay (view, 1, LRG_EDITOR_VIEW_OVERLAY_SHADOWS));
	g_assert_true (lrg_editor_view_state_has_overlay (view, 0, LRG_EDITOR_VIEW_OVERLAY_SHADOWS));
	g_clear_pointer (&r, json_node_unref);
	r = efx_ipc_result (f, "{'id':1,'method':'view.overlay','params':{'viewport':1,'name':'post'}}");
	g_assert_true (lrg_editor_view_state_has_overlay (view, 1, LRG_EDITOR_VIEW_OVERLAY_POST));
	{
		g_autofree gchar *reply = efx_ipc (f, "{'id':1,'method':'view.overlay','params':{'name':'fog'}}");

		g_assert_nonnull (strstr (reply, "\"error\""));
	}

	/* The header's render mode combo writes back */
	lrg_combo_box_set_selected (LRG_COMBO_BOX (efx_find (f, "vp3-render-mode")), LRG_EDITOR_RENDER_MODE_WIREFRAME);
	g_assert_cmpint (lrg_editor_view_state_get_render_mode (view, 3), ==, LRG_EDITOR_RENDER_MODE_WIREFRAME);

	/* ...and the view preset combo moves that viewport's camera only */
	lrg_combo_box_set_selected (LRG_COMBO_BOX (efx_find (f, "vp0-view-preset")), 2 /* Top */);
	g_assert_cmpint (lrg_editor_view_state_get_camera (view, 0)->axis_view, ==, LRG_EDITOR_AXIS_VIEW_TOP);
	g_assert_cmpint (lrg_editor_view_state_get_camera (view, 2)->axis_view, ==, LRG_EDITOR_AXIS_VIEW_FRONT);
	(void) split;
}

/* ==========================================================================
 * Drops, camera preview, gizmo drags, render on demand
 * ========================================================================== */

static void
test_asset_drop (EditorAppFixture *f,
                 gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewportPanel *panel = lrg_editor_viewport_split_get_panel (split, 0);
	LrgEditorViewState     *view = lrg_editor_app_get_view_state (f->app);
	LrgEditorViewCamera    *cam = lrg_editor_view_state_get_camera (view, 0);
	LrgNode                *node;
	GrlVector3             *loc;
	gint64                  undo;
	gfloat                  x, y;

	/* Look straight down at the origin so the picked point is known */
	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_TOP);
	lrg_editor_view_state_camera_changed (view, 0);
	efx_frames (f, 2);

	undo = undo_count (f);
	node = lrg_editor_viewport_panel_drop_asset (panel, "models/crate.glb", cam->view.width * 0.5f,
	                                             cam->view.height * 0.5f);
	g_assert_nonnull (node);
	g_assert_cmpstr (lrg_node_get_name (node), ==, "crate");
	g_assert_cmpstr (lrg_node_visual_get_asset (lrg_node_get_visual (node)), ==, "models/crate.glb");
	loc = lrg_node_get_location (node);
	g_assert_cmpfloat (fabsf (loc->y), <, 1e-3f);
	g_assert_cmpfloat (fabsf (loc->x - cam->view.target[0]), <, 0.05f);
	g_assert_cmpfloat (fabsf (loc->z - cam->view.target[2]), <, 0.05f);
	g_assert_cmpint (undo_count (f), ==, undo + 1);

	/* The same through a real drag-and-drop session onto the viewport */
	viewport_centre (panel, &x, &y);
	efx_move (f, x - 200.0f, y);
	efx_button (f, x - 200.0f, y, GRL_MOUSE_BUTTON_LEFT, TRUE);
	{
		g_autoptr(LrgDragPayload) payload = lrg_drag_payload_new_string ("application/x-lrg-asset",
		                                                                 "textures/barrel.png", LRG_DRAG_ACTION_COPY);

		g_assert_true (lrg_canvas_begin_drag (lrg_editor_app_get_canvas (f->app), NULL, payload, x - 200.0f, y));
	}
	efx_move (f, x - 100.0f, y);
	efx_move (f, x, y);
	efx_move (f, x + 1.0f, y);
	efx_button (f, x + 1.0f, y, GRL_MOUSE_BUTTON_LEFT, FALSE);
	efx_frames (f, 2);
	node = lrg_editor_find_node (f->editor, "barrel");
	g_assert_nonnull (node);
	g_assert_cmpint (lrg_node_visual_get_kind (lrg_node_get_visual (node)), ==, LRG_NODE_VISUAL_SPRITE);
	g_assert_cmpint (undo_count (f), ==, undo + 2);
}

static void
test_camera_preview (EditorAppFixture *f,
                     gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewportPanel *panel = lrg_editor_viewport_split_get_panel (split, 0);
	LrgNode                *cam_node;
	LrgEditorView           v;
	gfloat                  px, py, pw, ph, vx, vy, vw, vh;

	cam_node = create_node (f, "{'name':'Cam','kind':'camera'}");
	g_assert_nonnull (cam_node);
	lrg_editor_select (f->editor, NULL, FALSE);
	efx_frames (f, 1);
	g_assert_null (lrg_editor_viewport_panel_get_preview_node (panel));

	lrg_editor_select (f->editor, cam_node, FALSE);
	efx_frames (f, 2);
	g_assert_true (lrg_editor_viewport_panel_get_preview_node (panel) == cam_node);
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (lrg_editor_viewport_panel_get_preview (panel))));

	/* Bottom right of the viewport, inside it, 16:9 */
	efx_rect_of (LRG_WIDGET (lrg_editor_viewport_panel_get_preview (panel)), &px, &py, &pw, &ph);
	efx_rect_of (LRG_WIDGET (lrg_editor_viewport_panel_get_viewport (panel)), &vx, &vy, &vw, &vh);
	g_assert_cmpfloat (px + pw, <=, vx + vw);
	g_assert_cmpfloat (py + ph, <=, vy + vh);
	g_assert_cmpfloat (px, >, vx + vw * 0.5f);
	g_assert_cmpfloat (fabsf (pw / ph - 16.0f / 9.0f), <, 0.05f);

	/* The view through the camera node: its position, looking down -Z */
	lrg_editor_scene_renderer_camera_node_view (cam_node, 160.0f, 90.0f, &v);
	g_assert_cmpfloat (v.target[2] - v.position[2], <, 0.0f);

	/* The header button hides it; selecting something else too */
	efx_click_widget (f, efx_find (f, "vp0-preview-toggle"));
	efx_frames (f, 1);
	g_assert_null (lrg_editor_viewport_panel_get_preview_node (panel));
	g_assert_false (lrg_widget_get_visible (LRG_WIDGET (lrg_editor_viewport_panel_get_preview (panel))));
	lrg_editor_viewport_panel_set_preview_enabled (panel, TRUE);
	efx_frames (f, 1);
	g_assert_nonnull (lrg_editor_viewport_panel_get_preview_node (panel));
	lrg_editor_select (f->editor, NULL, FALSE);
	efx_frames (f, 1);
	g_assert_null (lrg_editor_viewport_panel_get_preview_node (panel));
}

static void
test_gizmo_drag (EditorAppFixture *f,
                 gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewportPanel *panel = lrg_editor_viewport_split_get_panel (split, 0);
	LrgEditorViewState     *view = lrg_editor_app_get_view_state (f->app);
	LrgNode                *box;
	LrgEditorView           v;
	gfloat                  vx, vy, vw, vh, o[3] = { 0.0f, 0.0f, 0.0f }, tip[3];
	gfloat                  ou, ov, tu, tv, sx, sy, ex, ey;
	gint64                  undo;
	g_autoptr(LrgEditorGizmo) gizmo = lrg_editor_gizmo_new (f->editor);

	box = create_node (f, "{'name':'Box','kind':'primitive','primitive':'cube'}");
	lrg_editor_select (f->editor, box, FALSE);
	lrg_editor_set_tool (f->editor, LRG_EDITOR_TOOL_TRANSLATE);
	efx_frames (f, 3);

	/* Where the X arm is on screen: 60% of the way to its tip */
	lrg_editor_view_state_get_view (view, 0, &v);
	tip[0] = lrg_editor_gizmo_get_size (gizmo, &v);
	tip[1] = tip[2] = 0.0f;
	g_assert_true (lrg_editor_view_project (&v, o, &ou, &ov, NULL));
	g_assert_true (lrg_editor_view_project (&v, tip, &tu, &tv, NULL));
	efx_rect_of (LRG_WIDGET (lrg_editor_viewport_panel_get_viewport (panel)), &vx, &vy, &vw, &vh);
	{
		GrlRectangle r;

		g_assert_true (lrg_viewport_get_display_rect (lrg_editor_viewport_panel_get_viewport (panel), &r));
		vx += r.x;
		vy += r.y;
	}
	sx = vx + ou + (tu - ou) * 0.6f;
	sy = vy + ov + (tv - ov) * 0.6f;
	ex = sx + (tu - ou) * 0.8f;
	ey = sy + (tv - ov) * 0.8f;

	/* The gizmo paints while visible (same geometry as the hit test) */
	{
		g_autoptr(LrgPaintList) list = lrg_paint_list_new ();

		lrg_editor_gizmo_paint_transform (list, gizmo, LRG_EDITOR_TOOL_TRANSLATE, &v,
		                                  vx, vy, 1.0f);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 10);
	}

	undo = undo_count (f);
	efx_move (f, sx, sy);
	efx_button (f, sx, sy, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_move (f, (sx + ex) * 0.5f, (sy + ey) * 0.5f);
	efx_move (f, ex, ey);
	efx_button (f, ex, ey, GRL_MOUSE_BUTTON_LEFT, FALSE);
	efx_frames (f, 1);

	g_assert_cmpfloat (lrg_node_get_location (box)->x, >, 0.05f);
	g_assert_cmpfloat (fabsf (lrg_node_get_location (box)->y), <, 1e-4f);
	g_assert_cmpint (undo_count (f), ==, undo + 1);
	g_assert_null (lrg_editor_gizmo_paint_format_delta (gizmo, LRG_EDITOR_TOOL_TRANSLATE));
}

static void
test_render_on_demand (EditorAppFixture *f,
                       gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewportPanel *p0 = lrg_editor_viewport_split_get_panel (split, 0);
	LrgEditorViewportPanel *p1 = lrg_editor_viewport_split_get_panel (split, 1);
	LrgEditorViewState     *view = lrg_editor_app_get_view_state (f->app);

	lrg_editor_view_state_set_layout (view, LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_H);
	efx_frames (f, 3);
	g_assert_false (lrg_editor_viewport_panel_get_render_pending (p0));
	g_assert_false (lrg_editor_viewport_panel_get_render_pending (p1));

	/* A camera change dirties only its viewport... */
	lrg_editor_view_state_zoom_step (view, TRUE);
	lrg_editor_view_state_set_active_viewport (view, 1);
	efx_frames (f, 1);
	lrg_editor_view_camera_zoom_step (lrg_editor_view_state_get_camera (view, 1), TRUE);
	lrg_editor_view_state_camera_changed (view, 1);
	g_assert_true (lrg_editor_viewport_panel_get_render_pending (p1));
	g_assert_false (lrg_editor_viewport_panel_get_render_pending (p0));
	efx_frames (f, 1);
	g_assert_false (lrg_editor_viewport_panel_get_render_pending (p1));

	/* ...a document change dirties all of them */
	create_node (f, "{'name':'Ball','kind':'primitive','primitive':'uv-sphere'}");
	g_assert_true (lrg_editor_viewport_panel_get_render_pending (p0));
	g_assert_true (lrg_editor_viewport_panel_get_render_pending (p1));
	efx_frames (f, 1);
	g_assert_false (lrg_editor_viewport_panel_get_render_pending (p0));
}

/* ==========================================================================
 * Lights, gizmo painting, shaders (headless)
 * ========================================================================== */

static void
test_light_set (EditorAppFixture *f,
                gconstpointer     data)
{
	g_autoptr(LrgEditorSceneRenderer) renderer = lrg_editor_scene_renderer_new (f->editor);
	LrgNode                          *sun;
	GValue                            v = G_VALUE_INIT;

	/* No lights: the editor sun only */
	g_assert_cmpuint (lrg_editor_scene_renderer_sync_lights (renderer, TRUE), ==, 0);
	g_assert_false (lrg_editor_scene_renderer_has_scene_sun (renderer));

	create_node (f, "{'name':'Lamp','kind':'light'}");
	create_node (f, "{'name':'Torch','kind':'light'}");
	sun = create_node (f, "{'name':'Sun','kind':'light'}");
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "directional");
	lrg_node_visual_set_param (lrg_node_get_visual (sun), "light_type", &v);
	g_value_unset (&v);

	g_assert_cmpuint (lrg_editor_scene_renderer_sync_lights (renderer, TRUE), ==, 3);
	g_assert_true (lrg_editor_scene_renderer_has_scene_sun (renderer));
	g_assert_cmpuint (lrg_light_set_get_point_light_count (lrg_editor_scene_renderer_get_light_set (renderer)), ==, 2);

	/* Scene lighting off: back to the default sun, no point lights */
	g_assert_cmpuint (lrg_editor_scene_renderer_sync_lights (renderer, FALSE), ==, 0);
	g_assert_false (lrg_editor_scene_renderer_has_scene_sun (renderer));
	g_assert_cmpuint (lrg_light_set_get_point_light_count (lrg_editor_scene_renderer_get_light_set (renderer)), ==, 0);

	/* The preview post chain holds the four engine effects */
	{
		LrgPostProcessor *proc = lrg_editor_scene_renderer_get_post_processor (renderer, 0);

		g_assert_nonnull (proc);
		g_assert_nonnull (lrg_post_processor_get_effect (proc, "fxaa"));
		g_assert_nonnull (lrg_post_processor_get_effect (proc, "bloom"));
		g_assert_nonnull (lrg_post_processor_get_effect (proc, "vignette"));
		g_assert_nonnull (lrg_post_processor_get_effect (proc, "color-grade"));
		g_assert_null (lrg_editor_scene_renderer_get_post_processor (renderer, 99));
	}
}

static void
test_missing_asset (EditorAppFixture *f,
                    gconstpointer     data)
{
	g_autoptr(LrgEditorSceneRenderer) renderer = lrg_editor_scene_renderer_new (f->editor);
	LrgNode                          *ghost = create_node (f, "{'name':'Ghost','kind':'mesh-asset'}");
	LrgNode                          *box = create_node (f, "{'name':'Box','kind':'primitive','primitive':'cube'}");

	lrg_node_visual_set_asset (lrg_node_get_visual (ghost), "/nonexistent/ghost.glb");
	g_assert_true (lrg_editor_scene_renderer_is_asset_missing (renderer, ghost));
	g_assert_false (lrg_editor_scene_renderer_is_asset_missing (renderer, box));
}

static void
test_gizmo_paint (void)
{
	g_autoptr(LrgPaintList)        list = lrg_paint_list_new ();
	g_autoptr(LrgEditorViewCamera) cam = lrg_editor_view_camera_new ();
	g_autoptr(LrgTheme)            theme = lrg_theme_new ();
	gfloat                         h;
	guint                          n;

	lrg_editor_view_camera_set_size (cam, 640.0f, 480.0f);

	/* Orientation axes: disc, three lines, six ends, three labels */
	lrg_editor_gizmo_paint_axes (list, theme, cam, -1, FALSE, 0.0f, 0.0f, 1.0f);
	n = lrg_paint_list_get_n_instances (list);
	g_assert_cmpuint (n, >=, 10);

	/* A 2D camera has no orientation gizmo */
	cam->view_2d = TRUE;
	lrg_paint_list_clear (list);
	lrg_editor_gizmo_paint_axes (list, theme, cam, -1, FALSE, 0.0f, 0.0f, 1.0f);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), ==, 0);

	/* Chips report their height; frames and box select record */
	lrg_paint_list_clear (list);
	h = lrg_editor_gizmo_paint_chip (list, theme, "12 nodes", 100.0f, 10.0f, TRUE, NULL);
	g_assert_cmpfloat (h, >, 0.0f);
	lrg_editor_gizmo_paint_box_select (list, theme, 10.0f, 10.0f, 50.0f, 40.0f);
	lrg_editor_gizmo_paint_play_frame (list, theme, 0.0f, 0.0f, 640.0f, 480.0f, TRUE);
	lrg_editor_gizmo_paint_active_outline (list, theme, 0.0f, 0.0f, 640.0f, 480.0f, 1.0f);
	g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 6);
}

/* data/editor/shaders of the source tree ($G_TEST_SRCDIR/.. under make
 * test, else the working directory). */
static gchar *
shader_dir (void)
{
	const gchar *srcdir = g_getenv ("G_TEST_SRCDIR");
	gchar       *dir;

	if (srcdir != NULL)
	{
		dir = g_build_filename (srcdir, "..", "data", "editor", "shaders", NULL);
		if (g_file_test (dir, G_FILE_TEST_IS_DIR))
			return dir;
		g_free (dir);
	}
	dir = g_build_filename (g_get_current_dir (), "data", "editor", "shaders", NULL);
	if (g_file_test (dir, G_FILE_TEST_IS_DIR))
		return dir;
	g_free (dir);
	return NULL;
}

static void
test_shader_sources (void)
{
	static const gchar *names[] = { "scene.vs", "lit.fs", "unlit.fs", "normals.fs" };
	g_autofree gchar   *dir = shader_dir ();
	guint               i;

	g_assert_null (lrg_editor_scene_renderer_get_shader_source ("nope.fs"));
	if (dir == NULL)
	{
		g_test_skip ("data/editor/shaders not found (run from the source tree)");
		return;
	}
	for (i = 0; i < G_N_ELEMENTS (names); i++)
	{
		g_autofree gchar *file = g_strconcat (names[i], ".glsl", NULL);
		g_autofree gchar *path = g_build_filename (dir, file, NULL);
		g_autofree gchar *text = NULL;

		g_assert_true (g_file_get_contents (path, &text, NULL, NULL));
		g_assert_cmpstr (lrg_editor_scene_renderer_get_shader_source (names[i]), ==, text);
	}
}

/* ==========================================================================
 * GPU (only under the isolated Xvfb)
 * ========================================================================== */

#define GPU_W (160)
#define GPU_H (120)

static gboolean gpu_ready = FALSE;

static gboolean
gpu_init (void)
{
	const gchar *display = g_getenv ("DISPLAY");

	/* Never on a live Wayland session: xrun.sh unsets WAYLAND_DISPLAY */
	if (display == NULL || *display == '\0' || g_getenv ("WAYLAND_DISPLAY") != NULL)
		return FALSE;
	SetTraceLogLevel (LOG_WARNING);
	SetConfigFlags (FLAG_WINDOW_HIDDEN);
	InitWindow (64, 64, "test-editor-app-viewport");
	return IsWindowReady ();
}

/* Renders @overlays / @mode through @view into a fresh image. */
static Image
gpu_render (LrgEditorSceneRenderer *renderer,
            const LrgEditorView    *view,
            guint                   slot,
            LrgEditorRenderMode     mode,
            LrgEditorViewOverlays   overlays)
{
	RenderTexture2D rt = LoadRenderTexture (GPU_W, GPU_H);
	Image           img;

	BeginTextureMode (rt);
	g_assert_true (lrg_editor_scene_renderer_render_view (renderer, view, FALSE, slot, mode, overlays, GPU_W, GPU_H));
	EndTextureMode ();
	img = LoadImageFromTexture (rt.texture);
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	UnloadRenderTexture (rt);
	return img;
}

/* Sum of absolute channel differences between two same-sized images. */
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

/* Pixels that differ from the corner (background) colour. */
static guint
non_background (Image img)
{
	const guint8 *p = img.data;
	guint         n = 0;
	gint          i;

	for (i = 0; i < img.width * img.height; i++)
		if (ABS ((gint) p[i * 4] - (gint) p[0]) + ABS ((gint) p[i * 4 + 1] - (gint) p[1]) +
		    ABS ((gint) p[i * 4 + 2] - (gint) p[2]) > 24)
			n++;
	return n;
}

static void
test_gpu_render (EditorAppFixture *f,
                 gconstpointer     data)
{
	g_autoptr(LrgEditorSceneRenderer) renderer = NULL;
	LrgEditorView                     view;
	Image                             modes[5];
	Image                             plain, sky, shadows, post;
	LrgNode                          *ground;
	LrgNode                          *box;
	const LrgEditorViewOverlays       base = LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING;
	gint                              i, j;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}

	/* A ground plane and a lifted cube, seen from above at an angle */
	ground = create_node (f, "{'name':'Ground','kind':'primitive','primitive':'plane'}");
	box = create_node (f, "{'name':'Box','kind':'primitive','primitive':'cube'}");
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'node.set','params':{'node':'Ground','scale':[8,1,8]}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'node.set','params':{'node':'Box','location':[0,1,0]}}"));
	g_assert_nonnull (ground);
	g_assert_nonnull (box);
	renderer = lrg_editor_scene_renderer_new (f->editor);
	lrg_editor_view_init_default (&view, GPU_W, GPU_H);
	view.position[0] = 5.0f;
	view.position[1] = 5.0f;
	view.position[2] = 6.0f;
	view.target[0] = view.target[1] = view.target[2] = 0.0f;

	/* Every render mode produces a different image */
	for (i = 0; i < 5; i++)
		modes[i] = gpu_render (renderer, &view, 0, (LrgEditorRenderMode) i, base);
	for (i = 0; i < 5; i++)
	{
		g_assert_cmpuint (non_background (modes[i]), >, 200);
		for (j = i + 1; j < 5; j++)
			g_assert_cmpuint (image_diff (modes[i], modes[j]), >, 1000);
	}

	/* Sky, shadows and post each change the lit image */
	plain = gpu_render (renderer, &view, 0, LRG_EDITOR_RENDER_MODE_LIT, base);
	sky = gpu_render (renderer, &view, 0, LRG_EDITOR_RENDER_MODE_LIT, base | LRG_EDITOR_VIEW_OVERLAY_SKY);
	shadows = gpu_render (renderer, &view, 0, LRG_EDITOR_RENDER_MODE_LIT, base | LRG_EDITOR_VIEW_OVERLAY_SHADOWS);
	post = gpu_render (renderer, &view, 0, LRG_EDITOR_RENDER_MODE_LIT, base | LRG_EDITOR_VIEW_OVERLAY_POST);
	g_assert_cmpuint (image_diff (plain, sky), >, 1000);
	g_assert_cmpuint (image_diff (plain, shadows), >, 500);
	g_assert_cmpuint (image_diff (plain, post), >, 500);
	g_assert_true (lrg_shadow_renderer_get_cascade_count (lrg_editor_scene_renderer_get_shadow_renderer (renderer)) > 0);

	for (i = 0; i < 5; i++)
		UnloadImage (modes[i]);
	UnloadImage (plain);
	UnloadImage (sky);
	UnloadImage (shadows);
	UnloadImage (post);
	lrg_editor_scene_renderer_release (renderer);
}

/*
 * The 0.9.0 debug views (overdraw, UV, light complexity, AO,
 * reflections) through the editor viewport: the editor's mode values map
 * 1:1 onto LrgWorld3DRenderMode, each draws something and they differ
 * from each other and from the lit view.
 */
static void
test_gpu_debug_modes (EditorAppFixture *f,
                      gconstpointer     data)
{
	g_autoptr(LrgEditorSceneRenderer) renderer = NULL;
	LrgEditorView                     view;
	Image                             lit, modes[5];
	const LrgEditorViewOverlays       base = LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING;
	gint                              i, j;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}

	g_assert_nonnull (create_node (f, "{'name':'Ground','kind':'primitive','primitive':'plane'}"));
	g_assert_nonnull (create_node (f, "{'name':'Box','kind':'primitive','primitive':'cube'}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'node.set','params':{'node':'Ground','scale':[8,1,8]}}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'node.set','params':{'node':'Box','location':[0,1,0]}}"));
	renderer = lrg_editor_scene_renderer_new (f->editor);
	lrg_editor_view_init_default (&view, GPU_W, GPU_H);
	view.position[0] = 5.0f;
	view.position[1] = 5.0f;
	view.position[2] = 6.0f;
	view.target[0] = view.target[1] = view.target[2] = 0.0f;

	lit = gpu_render (renderer, &view, 0, LRG_EDITOR_RENDER_MODE_LIT, base);
	for (i = 0; i < 5; i++)
		modes[i] = gpu_render (renderer, &view, 0,
		                       (LrgEditorRenderMode) (LRG_WORLD3D_RENDER_MODE_OVERDRAW + i), base);
	for (i = 0; i < 5; i++)
	{
		g_assert_cmpuint (image_diff (lit, modes[i]), >, 1000);
		for (j = i + 1; j < 5; j++)
			g_assert_cmpuint (image_diff (modes[i], modes[j]), >, 1000);
	}
	/* overdraw, UV, light complexity (no point light: the dark ramp end)
	 * and AO (white without SSAO) draw the scene; reflections without an
	 * environment is black */
	g_assert_cmpuint (non_background (modes[1]), >, 200);
	g_assert_cmpuint (non_background (modes[3]), >, 200);

	UnloadImage (lit);
	for (i = 0; i < 5; i++)
		UnloadImage (modes[i]);
	lrg_editor_scene_renderer_release (renderer);
}

/*
 * The level look in the editor viewport: the level environment's sky
 * replaces the dome, a decal node projects onto the ground, and a
 * viewport's render-layer mask hides a node's layer.
 */
static void
test_gpu_level_look (EditorAppFixture *f,
                     gconstpointer     data)
{
	g_autoptr(LrgEditorSceneRenderer) renderer = NULL;
	LrgEditorView                     view;
	LrgEnvironment                   *env;
	LrgNode                          *decal;
	Image                             plain, look, decaled, culled;
	const LrgEditorViewOverlays       base = LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING | LRG_EDITOR_VIEW_OVERLAY_SKY;
	const gfloat                      zenith[3] = { 0.9f, 0.3f, 0.1f };
	const gfloat                      horizon[3] = { 0.9f, 0.6f, 0.3f };
	const gfloat                      ground[3] = { 0.2f, 0.2f, 0.2f };

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}

	g_assert_nonnull (create_node (f, "{'name':'Ground','kind':'primitive','primitive':'plane'}"));
	json_node_unref (efx_ipc_result (f, "{'id':1,'method':'node.set','params':{'node':'Ground','scale':[8,1,8]}}"));
	renderer = lrg_editor_scene_renderer_new (f->editor);
	lrg_editor_view_init_default (&view, GPU_W, GPU_H);
	view.position[0] = 5.0f;
	view.position[1] = 3.0f;
	view.position[2] = 6.0f;
	view.target[0] = view.target[1] = view.target[2] = 0.0f;
	plain = gpu_render (renderer, &view, 0, LRG_EDITOR_RENDER_MODE_LIT, base);

	/* An orange gradient environment on the level: the sky follows */
	env = lrg_level_ensure_environment (lrg_editor_get_level (f->editor));
	lrg_environment_set_sky_source (env, LRG_ENVIRONMENT_SKY_GRADIENT);
	lrg_environment_set_gradient (env, zenith, horizon, ground);
	lrg_environment_set_resolution (env, 32);
	look = gpu_render (renderer, &view, 0, LRG_EDITOR_RENDER_MODE_LIT, base);
	{
		const guint8 *top = (const guint8 *) look.data + ((gsize) (GPU_H - 3) * GPU_W + GPU_W / 2) * 4;

		/* (render textures read back bottom up: row GPU_H - 3 is the top) */
		g_test_message ("level sky top %d,%d,%d", top[0], top[1], top[2]);
		g_assert_cmpint (top[0], >, top[2] + 30);
	}
	g_assert_true (lrg_world3d_renderer_get_active_environment (
		lrg_editor_scene_renderer_get_runtime_renderer (renderer)) == env);

	/* A red decal node on the ground */
	decal = create_node (f, "{'name':'Decal','kind':'decal'}");
	g_assert_nonnull (decal);
	{
		LrgNodeVisual *visual = lrg_node_get_visual (decal);
		g_autoptr(LrgMaterial3D) red = lrg_material3d_new ();

		lrg_material3d_set_color (red, 0.9f, 0.05f, 0.05f, 1.0f);
		lrg_node_visual_set_material (visual, red);
		lrg_node_visual_set_param_double (visual, "size_x", 3.0);
		lrg_node_visual_set_param_double (visual, "size_y", 1.0);
		lrg_node_visual_set_param_double (visual, "size_z", 3.0);
	}
	decaled = gpu_render (renderer, &view, 0, LRG_EDITOR_RENDER_MODE_LIT, base);
	g_assert_cmpuint (lrg_decal_set_get_count (lrg_world3d_renderer_get_decal_set (
		lrg_editor_scene_renderer_get_runtime_renderer (renderer))), ==, 1);
	g_assert_cmpuint (image_diff (look, decaled), >, 2000);

	/* The viewport mask: layer 0 hidden, the ground goes */
	g_assert_cmpuint (lrg_editor_scene_renderer_get_cull_mask (renderer, 0), ==, LRG_RENDER_LAYERS_MASK_ALL);
	lrg_editor_scene_renderer_set_cull_mask (renderer, 0, LRG_RENDER_LAYERS_MASK_ALL & ~LRG_RENDER_LAYERS_BIT (0));
	culled = gpu_render (renderer, &view, 0, LRG_EDITOR_RENDER_MODE_LIT, base);
	g_assert_cmpuint (image_diff (decaled, culled), >, 2000);
	g_assert_cmpuint (lrg_editor_scene_renderer_get_cull_mask (renderer, 1), ==, LRG_RENDER_LAYERS_MASK_ALL);
	lrg_editor_scene_renderer_set_cull_mask (renderer, 0, LRG_RENDER_LAYERS_MASK_ALL);

	lrg_level_set_environment (lrg_editor_get_level (f->editor), NULL);
	UnloadImage (plain);
	UnloadImage (look);
	UnloadImage (decaled);
	UnloadImage (culled);
	lrg_editor_scene_renderer_release (renderer);
}

/* ==========================================================================
 * 0.9.0 (ED2): view modes menu, render layers, volume gizmos, material drop
 * ========================================================================== */

/* The render-mode combo lists every mode, the debug views included. */
static void
test_ed2_view_modes (EditorAppFixture *f,
                     gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewState     *view = lrg_editor_app_get_view_state (f->app);
	LrgComboBox            *combo = LRG_COMBO_BOX (efx_find (f, "vp0-render-mode"));
	g_autofree gchar       *reply = NULL;

	g_assert_nonnull (split);
	g_assert_nonnull (combo);
	g_assert_cmpuint (lrg_combo_box_get_n_items (combo), ==, 10);
	lrg_combo_box_set_selected (combo, (gint) LRG_EDITOR_RENDER_MODE_OVERDRAW);
	g_assert_cmpint (lrg_editor_view_state_get_render_mode (view, 0), ==, LRG_EDITOR_RENDER_MODE_OVERDRAW);

	/* IPC: by nick, and the list of modes */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':1,'method':'view.render_mode','params':{'viewport':0,'mode':'light-complexity'}}");
		JsonObject         *o = json_node_get_object (r);

		g_assert_cmpstr (json_object_get_string_member (o, "mode"), ==, "light-complexity");
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "modes")), ==, 10);
	}
	g_assert_cmpint (lrg_editor_view_state_get_render_mode (view, 0), ==, LRG_EDITOR_RENDER_MODE_LIGHT_COMPLEXITY);
	efx_frames (f, 2);
	g_assert_cmpint (lrg_combo_box_get_selected (combo), ==, (gint) LRG_EDITOR_RENDER_MODE_LIGHT_COMPLEXITY);
	reply = efx_ipc (f, "{'id':2,'method':'view.render_mode','params':{'mode':'xray'}}");
	g_assert_nonnull (strstr (reply, "reflections"));
}

/* The layers button and view.layers drive the per-viewport cull mask. */
static void
test_ed2_layers (EditorAppFixture *f,
                 gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewportPanel *panel = lrg_editor_viewport_split_get_panel (split, 0);

	g_assert_cmpuint (lrg_editor_viewport_panel_get_layer_mask (panel), ==, LRG_RENDER_LAYERS_MASK_ALL);

	/* the header button opens the layers menu */
	efx_click_widget (f, efx_find (f, "vp0-layers"));
	efx_frames (f, 2);
	g_assert_nonnull (efx_find (f, "vp0-layers"));

	/* a layer toggle from the menu's action */
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (panel), "vp.layer-0", NULL));
	g_assert_cmpuint (lrg_editor_viewport_panel_get_layer_mask (panel) & LRG_RENDER_LAYERS_BIT (0), ==, 0);
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (panel), "vp.layers-none", NULL));
	g_assert_cmpuint (lrg_editor_viewport_panel_get_layer_mask (panel), ==, LRG_RENDER_LAYERS_MASK_NONE);
	g_assert_true (lrg_widget_activate_action (LRG_WIDGET (panel), "vp.layers-all", NULL));

	/* IPC: by name, by index, whole masks */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':1,'method':'view.layers','params':{'viewport':0,'layer':'water','enabled':false}}");
		JsonObject         *o = json_node_get_object (r);

		g_assert_cmpint (json_object_get_int_member (o, "mask"), ==,
		                 (gint64) (LRG_RENDER_LAYERS_MASK_ALL & ~LRG_RENDER_LAYERS_BIT (LRG_RENDER_LAYERS_WATER)));
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (o, "layers")), ==, 32);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (
			f, "{'id':2,'method':'view.layers','params':{'viewport':0,'layer':9}}");
	}
	g_assert_cmpuint (lrg_editor_viewport_panel_get_layer_mask (panel) & LRG_RENDER_LAYERS_BIT (9), ==, 0);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':3,'method':'view.layers','params':{'mask':'all'}}");
	}
	g_assert_cmpuint (lrg_editor_viewport_panel_get_layer_mask (panel), ==, LRG_RENDER_LAYERS_MASK_ALL);
	{
		g_autofree gchar *e = efx_ipc (f, "{'id':4,'method':'view.layers','params':{'layer':40}}");

		g_assert_nonnull (strstr (e, "\"error\""));
	}
}

/* Viewport pixel -> canvas point for panel 0. */
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

/* The volume handles: math, a drag on the canvas, undo, Escape. */
static void
test_ed2_volume_gizmo (EditorAppFixture *f,
                       gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewState     *vs = lrg_editor_app_get_view_state (f->app);
	LrgEditorView           view;
	LrgNode                *light, *decal, *probe;
	gfloat                  p[3], u, v, x, y;
	gdouble                 value;

	g_assert_nonnull (split);
	light = create_node (f, "{'kind':'light','name':'lamp'}");
	g_assert_nonnull (light);
	lrg_node_set_location_xyz (light, 0.0f, 0.0f, 0.0f);
	lrg_node_visual_set_param_double (lrg_node_get_visual (light), "range", 3.0);
	lrg_editor_select (f->editor, light, FALSE);
	efx_frames (f, 3);
	g_assert_true (lrg_editor_volume_has_gizmo (light));
	lrg_editor_view_state_get_view (vs, 0, &view);

	/* the range handle sits on the sphere; hit test finds it */
	g_assert_true (lrg_editor_volume_get_handle_position (&view, light, LRG_EDITOR_VOLUME_HANDLE_RANGE, p));
	g_assert_cmpfloat_with_epsilon (sqrtf (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]), 3.0f, 1e-3f);
	g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
	g_assert_cmpint (lrg_editor_volume_hit_test (&view, light, u, v), ==, LRG_EDITOR_VOLUME_HANDLE_RANGE);
	g_assert_cmpint (lrg_editor_volume_hit_test (&view, light, u + 40.0f, v + 40.0f), ==,
	                 LRG_EDITOR_VOLUME_HANDLE_NONE);
	g_assert_false (lrg_editor_volume_get_handle_position (&view, light, LRG_EDITOR_VOLUME_HANDLE_CONE, p));

	/* drag it outwards on the canvas: the range grows, one undo step */
	vp_to_canvas (f, u, v, &x, &y);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x + 60.0f, y, 4);
	value = lrg_node_visual_get_param_double (lrg_node_get_visual (light), "range", 0.0);
	g_assert_cmpfloat (value, >, 3.2);
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (lrg_node_get_visual (light), "range", 0.0), 3.0,
	                                1e-6);

	/* Escape during a drag puts the old value back */
	efx_move (f, x, y);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_move (f, x + 50.0f, y);
	efx_key (f, GRL_KEY_ESCAPE, LRG_INPUT_MODIFIER_NONE);
	efx_button (f, x + 50.0f, y, GRL_MOUSE_BUTTON_LEFT, FALSE);
	g_assert_cmpfloat_with_epsilon (lrg_node_visual_get_param_double (lrg_node_get_visual (light), "range", 0.0), 3.0,
	                                1e-6);

	/* a spot cone: the angle follows the pointer on the base plane */
	{
		g_auto(GValue) s = G_VALUE_INIT;
		gfloat         rim[3];

		g_value_init (&s, G_TYPE_STRING);
		g_value_set_string (&s, "spot");
		lrg_node_visual_set_param (lrg_node_get_visual (light), "light_type", &s);
		lrg_node_visual_set_param_double (lrg_node_get_visual (light), "spot_angle", 60.0);
		g_assert_true (lrg_editor_volume_get_handle_position (&view, light, LRG_EDITOR_VOLUME_HANDLE_CONE, rim));
		g_assert_true (lrg_editor_view_project (&view, rim, &u, &v, NULL));
		g_assert_true (lrg_editor_volume_drag_value (&view, light, LRG_EDITOR_VOLUME_HANDLE_CONE, u, v, &value));
		g_assert_cmpfloat_with_epsilon (value, 60.0, 0.5);
		g_assert_true (lrg_editor_volume_drag_value (&view, light, LRG_EDITOR_VOLUME_HANDLE_INNER_CONE, u, v, &value));
		g_assert_cmpfloat (value, <=, 60.0);
	}

	/* a decal box (sizes over the node's scale) and a probe box */
	decal = create_node (f, "{'kind':'decal','name':'stain'}");
	g_assert_nonnull (decal);
	lrg_node_set_location_xyz (decal, 0.0f, 0.0f, 0.0f);
	lrg_node_set_scale_xyz (decal, 2.0f, 1.0f, 1.0f);
	lrg_node_visual_set_param_double (lrg_node_get_visual (decal), "size_x", 1.5);
	g_assert_true (lrg_editor_volume_get_handle_position (&view, decal, LRG_EDITOR_VOLUME_HANDLE_SIZE_X, p));
	g_assert_cmpfloat_with_epsilon (p[0], 1.5f, 1e-3f);
	g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
	g_assert_true (lrg_editor_volume_drag_value (&view, decal, LRG_EDITOR_VOLUME_HANDLE_SIZE_X, u, v, &value));
	g_assert_cmpfloat_with_epsilon (value, 1.5, 0.02);

	probe = create_node (f, "{'kind':'reflection-probe','name':'mirror'}");
	g_assert_nonnull (probe);
	lrg_node_set_location_xyz (probe, 1.0f, 0.0f, 0.0f);
	g_assert_true (lrg_editor_volume_get_handle_position (&view, probe, LRG_EDITOR_VOLUME_HANDLE_EXTENT_Y, p));
	g_assert_cmpfloat_with_epsilon (p[1], 5.0f, 1e-3f);
	g_assert_cmpstr (lrg_editor_volume_handle_get_param (LRG_EDITOR_VOLUME_HANDLE_EXTENT_Y), ==, "extents_y");

	/* the overlay paints volumes without a GPU */
	lrg_editor_select (f->editor, probe, FALSE);
	efx_frames (f, 2);
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
}

/* Dropping a .rmat on a mesh assigns it (ED1's undoable command). */
static void
test_ed2_material_drop (EditorAppFixture *f,
                        gconstpointer     data)
{
	g_autoptr(LrgWidget)    old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewportPanel *panel = lrg_editor_viewport_split_get_panel (split, 0);
	LrgEditorViewState     *vs = lrg_editor_app_get_view_state (f->app);
	LrgEditorViewCamera    *cam = lrg_editor_view_state_get_camera (vs, 0);
	g_autoptr(GError)       error = NULL;
	g_autofree gchar       *mat = NULL;
	LrgNode                *cube;
	LrgNode                *hit;
	gint64                  undo;
	guint                   before;

	g_assert_true (lrg_editor_app_new_project (f->app, f->project, "Mat", &error));
	g_assert_no_error (error);
	efx_frames (f, 2);
	split = fx_split (f, &old);
	panel = lrg_editor_viewport_split_get_panel (split, 0);
	cam = lrg_editor_view_state_get_camera (vs, 0);
	mat = g_build_filename (f->project, "steel.rmat", NULL);
	g_assert_true (g_file_set_contents (mat, "name: steel\nmetallic: 1.0\nroughness: 0.3\n", -1, NULL));

	cube = create_node (f, "{'primitive':'cube','name':'block'}");
	lrg_node_set_location_xyz (cube, 0.0f, 0.0f, 0.0f);
	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_TOP);
	lrg_editor_view_camera_frame_all (cam, lrg_editor_get_level (f->editor), 50.0f);
	lrg_editor_view_state_camera_changed (vs, 0);
	efx_frames (f, 2);

	before = lrg_level_count_nodes (lrg_editor_get_level (f->editor));
	undo = undo_count (f);
	hit = lrg_editor_viewport_panel_drop_asset (panel, mat, cam->view.width * 0.5f, cam->view.height * 0.5f);
	g_assert_true (hit == cube);
	g_assert_nonnull (lrg_node_visual_get_material_ref (lrg_node_get_visual (cube)));
	g_assert_cmpuint (lrg_level_count_nodes (lrg_editor_get_level (f->editor)), ==, before);
	g_assert_cmpint (undo_count (f), ==, undo + 1);
	lrg_editor_undo (f->editor);
	g_assert_null (lrg_node_visual_get_material_ref (lrg_node_get_visual (cube)));

	/* nothing under the pointer: no node, no command */
	g_assert_null (lrg_editor_viewport_panel_drop_asset (panel, mat, 2.0f, 2.0f));
	g_assert_cmpint (undo_count (f), ==, undo);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	gpu_ready = gpu_init ();

	g_test_add ("/editor-app/viewport/layouts", EditorAppFixture, NULL, efx_setup, test_layouts, efx_teardown);
	g_test_add ("/editor-app/viewport/maximize", EditorAppFixture, NULL, efx_setup, test_maximize_button,
	            efx_teardown);
	g_test_add ("/editor-app/viewport/active", EditorAppFixture, NULL, efx_setup, test_active_viewport, efx_teardown);
	g_test_add ("/editor-app/viewport/cameras", EditorAppFixture, NULL, efx_setup, test_camera_independence,
	            efx_teardown);
	g_test_add ("/editor-app/viewport/mode-overlay-ipc", EditorAppFixture, NULL, efx_setup,
	            test_render_mode_overlay_ipc, efx_teardown);
	g_test_add ("/editor-app/viewport/asset-drop", EditorAppFixture, NULL, efx_setup, test_asset_drop, efx_teardown);
	g_test_add ("/editor-app/viewport/camera-preview", EditorAppFixture, NULL, efx_setup, test_camera_preview,
	            efx_teardown);
	g_test_add ("/editor-app/viewport/gizmo-drag", EditorAppFixture, NULL, efx_setup, test_gizmo_drag, efx_teardown);
	g_test_add ("/editor-app/viewport/render-on-demand", EditorAppFixture, NULL, efx_setup, test_render_on_demand,
	            efx_teardown);
	g_test_add ("/editor-app/viewport/light-set", EditorAppFixture, NULL, efx_setup, test_light_set, efx_teardown);
	g_test_add ("/editor-app/viewport/missing-asset", EditorAppFixture, NULL, efx_setup, test_missing_asset,
	            efx_teardown);
	g_test_add_func ("/editor-app/viewport/gizmo-paint", test_gizmo_paint);
	g_test_add_func ("/editor-app/viewport/shader-sources", test_shader_sources);
	g_test_add ("/editor-app/viewport/gpu-render", EditorAppFixture, NULL, efx_setup, test_gpu_render, efx_teardown);
	g_test_add ("/editor-app/viewport/gpu-debug-modes", EditorAppFixture, NULL, efx_setup, test_gpu_debug_modes,
	            efx_teardown);
	g_test_add ("/editor-app/viewport/gpu-level-look", EditorAppFixture, NULL, efx_setup, test_gpu_level_look,
	            efx_teardown);

	g_test_add ("/editor-app/viewport/ed2-view-modes", EditorAppFixture, NULL, efx_setup, test_ed2_view_modes,
	            efx_teardown);
	g_test_add ("/editor-app/viewport/ed2-layers", EditorAppFixture, NULL, efx_setup, test_ed2_layers, efx_teardown);
	g_test_add ("/editor-app/viewport/ed2-volume-gizmo", EditorAppFixture, NULL, efx_setup, test_ed2_volume_gizmo,
	            efx_teardown);
	g_test_add ("/editor-app/viewport/ed2-material-drop", EditorAppFixture, NULL, efx_setup, test_ed2_material_drop,
	            efx_teardown);

	result = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return result;
}
