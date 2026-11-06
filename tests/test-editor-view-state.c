/* test-editor-view-state.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless tests for LrgEditorViewState and LrgEditorViewCamera: notifying
 * properties, layouts, per-viewport render modes and overlays, camera
 * conversions and string round trips, axis views with auto-perspective,
 * focus selection / frame all, 2D mode and the settings integration.
 * The camera operations are checked against the raw LrgEditorView math
 * the old immediate-mode app used, so behaviour is provably unchanged.
 */

#include <glib.h>
#include <string.h>
#include <math.h>
#include <libregnum.h>

#define EPS (1e-4f)

/* Counts notify emissions per property name. */
typedef struct
{
	GHashTable *counts;   /* name -> GUINT_TO_POINTER (count) */
} NotifyLog;

static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           gpointer    user_data)
{
	NotifyLog *log = user_data;
	guint      n = GPOINTER_TO_UINT (g_hash_table_lookup (log->counts, pspec->name));

	g_hash_table_insert (log->counts, (gpointer) pspec->name, GUINT_TO_POINTER (n + 1));
}

static guint
notified (NotifyLog   *log,
          const gchar *name)
{
	return GPOINTER_TO_UINT (g_hash_table_lookup (log->counts, name));
}

static void
on_count (gpointer instance,
          guint    index,
          gpointer user_data)
{
	(*(guint *) user_data)++;
}

/* ==========================================================================
 * Properties and notify
 * ========================================================================== */

static void
test_view_state_properties (void)
{
	g_autoptr(LrgEditorViewState) vs = lrg_editor_view_state_new (NULL);
	NotifyLog                     log;
	guint                         viewport_changes = 0;

	log.counts = g_hash_table_new (g_str_hash, g_str_equal);
	g_signal_connect (vs, "notify", G_CALLBACK (on_notify), &log);
	g_signal_connect (vs, "viewport-changed", G_CALLBACK (on_count), &viewport_changes);

	/* Defaults. */
	g_assert_cmpint (lrg_editor_view_state_get_layout (vs), ==, LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE);
	g_assert_cmpuint (lrg_editor_view_state_get_viewport_count (vs), ==, 1);
	g_assert_cmpuint (lrg_editor_view_state_get_active_viewport (vs), ==, 0);
	g_assert_cmpint (lrg_editor_view_state_get_nav_mode (vs), ==, LRG_EDITOR_NAV_MODE_NONE);
	g_assert_cmpint (lrg_editor_view_state_get_play_state (vs), ==, LRG_EDITOR_PLAY_STATE_STOPPED);
	g_assert_null (lrg_editor_view_state_get_dialog (vs));
	g_assert_true (lrg_editor_view_state_has_overlay (vs, -1, LRG_EDITOR_VIEW_OVERLAY_GRID));
	g_assert_false (lrg_editor_view_state_has_overlay (vs, -1, LRG_EDITOR_VIEW_OVERLAY_POST));

	/* Layout: quad shows four viewports. */
	lrg_editor_view_state_set_layout (vs, LRG_EDITOR_VIEWPORT_LAYOUT_QUAD);
	g_assert_cmpuint (notified (&log, "layout"), ==, 1);
	g_assert_cmpuint (notified (&log, "layout-name"), ==, 1);
	g_assert_cmpuint (notified (&log, "viewport-count"), ==, 1);
	g_assert_cmpstr (lrg_editor_view_state_get_layout_name (vs), ==, "quad");
	g_assert_cmpuint (lrg_editor_view_state_get_viewport_count (vs), ==, 4);

	/* Setting the same value notifies nothing. */
	lrg_editor_view_state_set_layout (vs, LRG_EDITOR_VIEWPORT_LAYOUT_QUAD);
	g_assert_cmpuint (notified (&log, "layout"), ==, 1);

	/* Active viewport, wrapping next-viewport, clamping on layout change. */
	lrg_editor_view_state_set_active_viewport (vs, 3);
	g_assert_cmpuint (lrg_editor_view_state_get_active_viewport (vs), ==, 3);
	g_assert_cmpuint (notified (&log, "camera"), ==, 1);
	lrg_editor_view_state_next_viewport (vs);
	g_assert_cmpuint (lrg_editor_view_state_get_active_viewport (vs), ==, 0);
	lrg_editor_view_state_set_active_viewport (vs, 2);
	g_assert_true (lrg_editor_view_state_set_layout_name (vs, "split-v"));
	g_assert_cmpint (lrg_editor_view_state_get_layout (vs), ==, LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_V);
	g_assert_cmpuint (lrg_editor_view_state_get_active_viewport (vs), ==, 1);
	g_assert_false (lrg_editor_view_state_set_layout_name (vs, "hexa"));
	g_assert_cmpint (lrg_editor_view_state_get_layout (vs), ==, LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_V);
	lrg_editor_view_state_set_active_viewport (vs, 9);
	g_assert_cmpuint (lrg_editor_view_state_get_active_viewport (vs), ==, 1);

	/* Per-viewport render mode and overlays (switching the active
	 * viewport above re-notified the active-viewport mirrors, so count
	 * from here). */
	g_hash_table_remove_all (log.counts);
	lrg_editor_view_state_set_render_mode (vs, 0, LRG_EDITOR_RENDER_MODE_WIREFRAME);
	g_assert_cmpuint (viewport_changes, ==, 1);
	g_assert_cmpint (lrg_editor_view_state_get_render_mode (vs, 0), ==, LRG_EDITOR_RENDER_MODE_WIREFRAME);
	g_assert_cmpint (lrg_editor_view_state_get_render_mode (vs, 1), ==, LRG_EDITOR_RENDER_MODE_LIT);
	g_assert_cmpuint (notified (&log, "render-mode"), ==, 0);   /* 0 is not active */
	lrg_editor_view_state_set_render_mode (vs, -1, LRG_EDITOR_RENDER_MODE_NORMALS);
	g_assert_cmpuint (notified (&log, "render-mode"), ==, 1);
	lrg_editor_view_state_toggle_overlay (vs, -1, LRG_EDITOR_VIEW_OVERLAY_GRID);
	g_assert_false (lrg_editor_view_state_has_overlay (vs, -1, LRG_EDITOR_VIEW_OVERLAY_GRID));
	g_assert_true (lrg_editor_view_state_has_overlay (vs, 0, LRG_EDITOR_VIEW_OVERLAY_GRID));
	g_assert_cmpuint (notified (&log, "overlays"), ==, 1);
	g_assert_cmpuint (viewport_changes, ==, 3);

	/* Switching the active viewport re-notifies its mirrors. */
	lrg_editor_view_state_set_active_viewport (vs, 0);
	g_assert_cmpuint (notified (&log, "render-mode"), ==, 2);
	g_assert_cmpint (lrg_editor_view_state_get_render_mode (vs, -1), ==, LRG_EDITOR_RENDER_MODE_WIREFRAME);

	/* Plain UI state, each notifying once per change. */
	lrg_editor_view_state_set_dialog (vs, "preferences");
	lrg_editor_view_state_set_dialog (vs, "preferences");
	g_assert_cmpstr (lrg_editor_view_state_get_dialog (vs), ==, "preferences");
	g_assert_cmpuint (notified (&log, "dialog"), ==, 1);
	lrg_editor_view_state_set_dialog (vs, NULL);
	g_assert_null (lrg_editor_view_state_get_dialog (vs));
	g_assert_cmpuint (notified (&log, "dialog"), ==, 2);

	lrg_editor_view_state_set_palette_open (vs, TRUE);
	lrg_editor_view_state_set_timeline_name (vs, "intro");
	lrg_editor_view_state_set_timeline_time (vs, -3.0f);   /* clamped to 0: no change */
	lrg_editor_view_state_set_timeline_time (vs, 1.5f);
	lrg_editor_view_state_set_timeline_playing (vs, TRUE);
	lrg_editor_view_state_set_timeline_previewing (vs, TRUE);
	lrg_editor_view_state_set_inspector_lock (vs, "guid-1");
	lrg_editor_view_state_set_status (vs, "Ready");
	lrg_editor_view_state_set_play_state (vs, LRG_EDITOR_PLAY_STATE_PAUSED);
	lrg_editor_view_state_set_nav_mode (vs, LRG_EDITOR_NAV_MODE_ORBIT);
	lrg_editor_view_state_set_tile_tool (vs, LRG_EDITOR_TILE_TOOL_FILL);
	lrg_editor_view_state_set_tile_mode (vs, TRUE);
	lrg_editor_view_state_set_fly_speed (vs, 9999.0);
	g_assert_cmpuint (notified (&log, "palette-open"), ==, 1);
	g_assert_cmpuint (notified (&log, "timeline-name"), ==, 1);
	g_assert_cmpuint (notified (&log, "timeline-time"), ==, 1);
	g_assert_cmpuint (notified (&log, "timeline-playing"), ==, 1);
	g_assert_cmpuint (notified (&log, "timeline-previewing"), ==, 1);
	g_assert_cmpuint (notified (&log, "inspector-lock"), ==, 1);
	g_assert_cmpuint (notified (&log, "status"), ==, 1);
	g_assert_cmpuint (notified (&log, "play-state"), ==, 1);
	g_assert_cmpuint (notified (&log, "nav-mode"), ==, 1);
	g_assert_cmpuint (notified (&log, "tile-tool"), ==, 1);
	g_assert_cmpuint (notified (&log, "tile-mode"), ==, 1);
	g_assert_cmpfloat (lrg_editor_view_state_get_timeline_time (vs), ==, 1.5f);
	g_assert_cmpfloat (lrg_editor_view_state_get_fly_speed (vs), ==, 500.0);
	g_assert_cmpstr (lrg_editor_view_state_get_inspector_lock (vs), ==, "guid-1");

	/* GObject property access goes through the same setters. */
	g_object_set (vs, "layout-name", "single", "status", "Saved", NULL);
	g_assert_cmpint (lrg_editor_view_state_get_layout (vs), ==, LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE);
	g_assert_cmpstr (lrg_editor_view_state_get_status (vs), ==, "Saved");

	g_hash_table_unref (log.counts);
}

static void
test_view_state_strings (void)
{
	static const gchar *navs[] = { "none", "fly", "orbit", "pan", "box", "zoom", "gizmo" };
	static const gchar *tools[] = { "brush", "eraser", "rect", "fill", "pick" };
	LrgEditorTileTool   tool;
	guint               i;

	/* The ui.state "nav" strings are unchanged from 0.8.0. */
	for (i = 0; i < G_N_ELEMENTS (navs); i++)
		g_assert_cmpstr (lrg_editor_nav_mode_to_string ((LrgEditorNavMode) i), ==, navs[i]);

	/* tilemap.tool names. */
	for (i = 0; i < G_N_ELEMENTS (tools); i++)
	{
		g_assert_cmpstr (lrg_editor_tile_tool_to_string ((LrgEditorTileTool) i), ==, tools[i]);
		g_assert_true (lrg_editor_tile_tool_from_string (tools[i], &tool));
		g_assert_cmpint (tool, ==, (gint) i);
	}
	g_assert_false (lrg_editor_tile_tool_from_string ("spray", &tool));
}

/* ==========================================================================
 * Camera math
 * ========================================================================== */

static void
test_camera_round_trips (void)
{
	g_autoptr(LrgEditorViewCamera) cam = lrg_editor_view_camera_new ();
	g_autoptr(LrgEditorViewCamera) back = NULL;
	g_autofree gchar              *text = NULL;
	LrgEditorView                  view;
	gfloat                         yaw, pitch, dist;

	lrg_editor_view_camera_set_size (cam, 640.0f, 480.0f);
	lrg_editor_view_camera_orbit (cam, 0.3f, -0.2f);
	lrg_editor_view_camera_pan (cam, 13.0f, -7.0f);

	/* to_view / from_view are lossless. */
	lrg_editor_view_camera_to_view (cam, &view);
	g_assert_cmpfloat (view.width, ==, 640.0f);
	back = lrg_editor_view_camera_new ();
	back->view_2d = cam->view_2d;
	lrg_editor_view_camera_from_view (back, &view);
	g_assert_true (lrg_editor_view_camera_equal (cam, back));

	/* The string form is exact (and keeps the target size). */
	text = lrg_editor_view_camera_to_string (cam);
	lrg_editor_view_camera_reset (back, 50.0f);
	g_assert_true (lrg_editor_view_camera_from_string (back, text));
	g_assert_true (lrg_editor_view_camera_equal (cam, back));
	g_assert_false (lrg_editor_view_camera_from_string (back, "1,2,3"));
	g_assert_false (lrg_editor_view_camera_from_string (back, "a,b,c,d,e,f,g,h,i,j,k,l"));
	g_assert_true (lrg_editor_view_camera_equal (cam, back));

	/* Orbit angles: derived values set back give the same pose. */
	yaw = lrg_editor_view_camera_get_yaw (cam);
	pitch = lrg_editor_view_camera_get_pitch (cam);
	dist = lrg_editor_view_camera_get_distance (cam);
	lrg_editor_view_camera_set_orbit (back, yaw, pitch, dist);
	g_assert_cmpfloat_with_epsilon (back->view.position[0], cam->view.position[0], EPS);
	g_assert_cmpfloat_with_epsilon (back->view.position[1], cam->view.position[1], EPS);
	g_assert_cmpfloat_with_epsilon (back->view.position[2], cam->view.position[2], EPS);

	/* A known pose: the default camera sits at (8, 6, 8). */
	lrg_editor_view_camera_reset (back, 50.0f);
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_yaw (back), (gfloat) G_PI / 4.0f, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_distance (back), sqrtf (164.0f), EPS);
	g_assert_cmpfloat (back->view.fovy, ==, 50.0f);
	g_assert_cmpfloat (back->view.width, ==, 640.0f);

	/* Boxed type copies. */
	{
		GValue               v = G_VALUE_INIT;
		LrgEditorViewCamera *copy;

		g_value_init (&v, LRG_TYPE_EDITOR_VIEW_CAMERA);
		g_value_set_boxed (&v, cam);
		copy = g_value_dup_boxed (&v);
		g_assert_true (lrg_editor_view_camera_equal (copy, cam));
		lrg_editor_view_camera_free (copy);
		g_value_unset (&v);
	}
}

/* The camera wrappers do exactly what the old app did with the raw view. */
static void
test_camera_matches_old_math (void)
{
	g_autoptr(LrgEditorViewCamera) cam = lrg_editor_view_camera_new ();
	LrgEditorView                  old;
	const gfloat                   top[3] = { 0.0f, 1.0f, 0.0f };

	lrg_editor_view_camera_set_size (cam, 800.0f, 600.0f);
	lrg_editor_view_camera_to_view (cam, &old);

	/* view.top: lrg_editor_view_align + ortho. */
	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_TOP);
	lrg_editor_view_align (&old, top);
	old.orthographic = TRUE;
	g_assert_cmpmem (cam->view.position, sizeof old.position, old.position, sizeof old.position);
	g_assert_cmpmem (cam->view.target, sizeof old.target, old.target, sizeof old.target);
	g_assert_true (cam->view.orthographic);

	/* view.orbit-left: back to perspective, then lrg_editor_view_orbit. */
	lrg_editor_view_camera_orbit_step (cam, -1, 0);
	old.orthographic = FALSE;
	lrg_editor_view_orbit (&old, LRG_EDITOR_VIEW_ORBIT_STEP, 0.0f);
	g_assert_cmpmem (cam->view.position, sizeof old.position, old.position, sizeof old.position);
	g_assert_false (cam->view.orthographic);

	/* view.zoom-in / zoom-out and a fly step. */
	lrg_editor_view_camera_zoom_step (cam, TRUE);
	lrg_editor_view_dolly (&old, 0.8f);
	lrg_editor_view_camera_zoom_step (cam, FALSE);
	lrg_editor_view_dolly (&old, 1.25f);
	lrg_editor_view_camera_fly (cam, 1.0f, 0.5f, 0.25f, 0.1f, -0.05f);
	lrg_editor_view_fly (&old, 1.0f, 0.5f, 0.25f, 0.1f, -0.05f);
	g_assert_cmpmem (cam->view.position, sizeof old.position, old.position, sizeof old.position);
	g_assert_cmpmem (cam->view.target, sizeof old.target, old.target, sizeof old.target);
}

static void
test_camera_axis_views (void)
{
	g_autoptr(LrgEditorViewCamera) cam = lrg_editor_view_camera_new ();
	gfloat                         m[16];
	gfloat                         dist;

	lrg_editor_view_camera_set_size (cam, 800.0f, 600.0f);
	dist = lrg_editor_view_camera_get_distance (cam);

	/* KP_7: straight down, orthographic, screen-right = +X exactly. */
	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_TOP);
	g_assert_cmpint (cam->axis_view, ==, LRG_EDITOR_AXIS_VIEW_TOP);
	g_assert_true (cam->view.orthographic);
	g_assert_true (cam->ortho_from_align);
	g_assert_cmpfloat (cam->view.position[0], ==, cam->view.target[0]);
	g_assert_cmpfloat (cam->view.position[2], ==, cam->view.target[2]);
	g_assert_cmpfloat_with_epsilon (cam->view.position[1] - cam->view.target[1], dist, EPS);
	lrg_editor_view_get_view_matrix (&cam->view, m);
	g_assert_cmpfloat (m[0], ==, 1.0f);   /* row 0 = camera right = +X */

	/* Ctrl variants look from the other side. */
	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_BOTTOM);
	g_assert_cmpfloat (cam->view.position[1] - cam->view.target[1], <, 0.0f);
	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_FRONT);
	g_assert_cmpfloat (cam->view.position[2] - cam->view.target[2], >, 0.0f);
	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_BACK);
	g_assert_cmpfloat (cam->view.position[2] - cam->view.target[2], <, 0.0f);
	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_RIGHT);
	g_assert_cmpfloat (cam->view.position[0] - cam->view.target[0], >, 0.0f);
	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_LEFT);
	g_assert_cmpint (cam->axis_view, ==, LRG_EDITOR_AXIS_VIEW_LEFT);
	g_assert_cmpfloat (cam->view.position[0] - cam->view.target[0], <, 0.0f);

	/* Panning keeps the axis view; orbiting leaves it for perspective. */
	lrg_editor_view_camera_pan (cam, 10.0f, 0.0f);
	g_assert_cmpint (cam->axis_view, ==, LRG_EDITOR_AXIS_VIEW_LEFT);
	lrg_editor_view_camera_orbit_step (cam, 1, 0);
	g_assert_cmpint (cam->axis_view, ==, LRG_EDITOR_AXIS_VIEW_NONE);
	g_assert_false (cam->view.orthographic);
	g_assert_false (cam->ortho_from_align);

	/* KP_5 is an explicit choice: orbiting keeps orthographic. */
	lrg_editor_view_camera_toggle_ortho (cam);
	g_assert_true (cam->view.orthographic);
	lrg_editor_view_camera_orbit_step (cam, 0, 1);
	g_assert_true (cam->view.orthographic);
	lrg_editor_view_camera_toggle_ortho (cam);
	g_assert_false (cam->view.orthographic);

	/* A vertical-only fly keeps the aligned ortho view; looking leaves it. */
	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_FRONT);
	lrg_editor_view_camera_fly (cam, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
	g_assert_true (cam->view.orthographic);
	lrg_editor_view_camera_fly (cam, 0.0f, 0.0f, 0.0f, 0.01f, 0.0f);
	g_assert_false (cam->view.orthographic);
}

static void
test_camera_2d (void)
{
	g_autoptr(LrgEditorViewCamera) cam = lrg_editor_view_camera_new ();
	g_autoptr(LrgEditorViewCamera) before = NULL;

	lrg_editor_view_camera_set_2d (cam, TRUE, 50.0f);
	g_assert_true (cam->view_2d);
	g_assert_true (cam->view.orthographic);
	g_assert_cmpfloat (cam->view.position[2], ==, 50.0f);
	g_assert_cmpfloat (cam->view.ortho_height, ==, 20.0f);

	/* In 2D, orbiting and the ortho toggle do nothing; aligning keeps
	 * the projection without auto-perspective. */
	before = lrg_editor_view_camera_copy (cam);
	lrg_editor_view_camera_orbit (cam, 1.0f, 1.0f);
	lrg_editor_view_camera_toggle_ortho (cam);
	g_assert_true (lrg_editor_view_camera_equal (before, cam));
	lrg_editor_view_camera_set_2d (cam, FALSE, 60.0f);
	g_assert_false (cam->view.orthographic);
	g_assert_cmpfloat (cam->view.fovy, ==, 60.0f);
}

/* ==========================================================================
 * Focus and frame
 * ========================================================================== */

static LrgNode *
add_cube (LrgEditor *editor,
          gfloat     x)
{
	g_autoptr(LrgNode)       node = lrg_node_new ("cube");
	g_autoptr(LrgNodeVisual) visual = lrg_node_visual_new (LRG_NODE_VISUAL_PRIMITIVE);

	lrg_node_visual_set_primitive (visual, LRG_PRIMITIVE_CUBE);
	lrg_node_set_visual (node, visual);
	lrg_node_set_location_xyz (node, x, 0.0f, 0.0f);
	lrg_editor_add_node (editor, node, NULL);
	return node;   /* the level holds a reference */
}

static void
test_view_state_focus_frame (void)
{
	g_autoptr(LrgEditor)          editor = lrg_editor_new ();
	g_autoptr(LrgEditorViewState) vs = lrg_editor_view_state_new (editor);
	g_autoptr(LrgEditorViewCamera) home = NULL;
	g_autoptr(GPtrArray)          one = g_ptr_array_new ();
	LrgEditorViewCamera          *cam;
	LrgNode                      *a, *b;
	gfloat                        center[3], radius;
	guint                         changes = 0;

	g_signal_connect (vs, "camera-changed", G_CALLBACK (on_count), &changes);
	lrg_editor_view_state_set_viewport_size (vs, -1, 800.0f, 600.0f);
	g_assert_cmpuint (changes, ==, 1);
	cam = lrg_editor_view_state_get_camera (vs, -1);
	home = lrg_editor_view_camera_copy (cam);

	/* Empty level: frame all resets the camera (no change from home). */
	lrg_editor_view_state_frame_all (vs);
	g_assert_true (lrg_editor_view_camera_equal (cam, home));
	g_assert_cmpuint (changes, ==, 1);

	/* Focus selection fits the selected node, keeping the direction. */
	a = add_cube (editor, 10.0f);
	b = add_cube (editor, -10.0f);
	lrg_editor_select (editor, a, FALSE);
	lrg_editor_view_state_focus_selection (vs);
	g_assert_cmpuint (changes, ==, 2);
	g_ptr_array_add (one, a);
	g_assert_true (lrg_editor_compute_bounds (one, center, &radius));
	g_assert_cmpfloat_with_epsilon (cam->view.target[0], center[0], EPS);
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_yaw (cam), lrg_editor_view_camera_get_yaw (home), EPS);

	/* Nothing selected: focus frames everything (target between both). */
	lrg_editor_select (editor, NULL, FALSE);
	lrg_editor_view_state_focus_selection (vs);
	g_assert_cmpfloat_with_epsilon (cam->view.target[0], 0.0f, 1e-3f);
	g_assert_cmpfloat (lrg_editor_view_camera_get_distance (cam), >,
	                   lrg_editor_view_camera_get_distance (home) * 0.5f);
	(void) b;

	/* Commands on the active camera only touch that viewport. */
	lrg_editor_view_state_set_layout (vs, LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_H);
	lrg_editor_view_state_set_active_viewport (vs, 1);
	{
		g_autoptr(LrgEditorViewCamera) first = lrg_editor_view_camera_copy (lrg_editor_view_state_get_camera (vs, 0));

		lrg_editor_view_state_zoom_step (vs, TRUE);
		lrg_editor_view_state_toggle_ortho (vs);
		lrg_editor_view_state_set_axis_view (vs, LRG_EDITOR_AXIS_VIEW_TOP);
		lrg_editor_view_state_orbit_step (vs, 1, 0);
		g_assert_true (lrg_editor_view_camera_equal (first, lrg_editor_view_state_get_camera (vs, 0)));
	}

	/* Toggle 2D and reset for a 2D level. */
	lrg_editor_view_state_toggle_2d (vs);
	g_assert_true (lrg_editor_view_state_get_camera (vs, -1)->view_2d);
	lrg_level_set_default_2d (lrg_editor_get_level (editor), TRUE);
	lrg_editor_view_state_reset_for_level (vs, lrg_editor_get_level (editor));
	g_assert_true (lrg_editor_view_state_get_camera (vs, 0)->view_2d);
	g_assert_true (lrg_editor_view_state_get_camera (vs, 3)->view_2d);
}

/* ==========================================================================
 * Settings
 * ========================================================================== */

static void
test_view_state_settings (void)
{
	g_autoptr(LrgEditorSettings)  settings = lrg_editor_settings_new ();
	g_autoptr(LrgEditorViewState) vs = lrg_editor_view_state_new (NULL);
	g_autoptr(LrgEditorViewState) other = lrg_editor_view_state_new (NULL);
	g_autofree gchar             *cams = NULL;
	gboolean                      grid = TRUE;
	guint                         i;

	g_object_set (settings, "camera-fov", 70.0, "camera-fly-speed", 12.0, "grid-visible", FALSE, NULL);
	lrg_editor_view_state_load_settings (vs, settings);
	g_assert_cmpfloat (lrg_editor_view_state_get_fov (vs), ==, 70.0);
	g_assert_cmpfloat (lrg_editor_view_state_get_fly_speed (vs), ==, 12.0);
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
		g_assert_false (lrg_editor_view_state_has_overlay (vs, (gint) i, LRG_EDITOR_VIEW_OVERLAY_GRID));

	/* Resetting uses the configured field of view. */
	lrg_editor_view_state_reset_camera (vs);
	g_assert_cmpfloat (lrg_editor_view_state_get_camera (vs, -1)->view.fovy, ==, 70.0f);

	lrg_editor_view_state_toggle_overlay (vs, 0, LRG_EDITOR_VIEW_OVERLAY_GRID);
	lrg_editor_view_state_save_settings (vs, settings);
	g_object_get (settings, "grid-visible", &grid, NULL);
	g_assert_true (grid);

	/* All four cameras round-trip through their string form. */
	lrg_editor_view_state_orbit_step (vs, 1, 1);
	lrg_editor_view_state_set_active_viewport (vs, 0);
	cams = lrg_editor_view_state_cameras_to_string (vs);
	g_assert_cmpuint (lrg_editor_view_state_cameras_from_string (other, cams), ==, 4);
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
		g_assert_true (lrg_editor_view_camera_equal (lrg_editor_view_state_get_camera (vs, (gint) i),
		                                             lrg_editor_view_state_get_camera (other, (gint) i)));
	g_assert_cmpuint (lrg_editor_view_state_cameras_from_string (other, "garbage"), ==, 0);
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/editor/view-state/properties", test_view_state_properties);
	g_test_add_func ("/editor/view-state/strings", test_view_state_strings);
	g_test_add_func ("/editor/view-state/camera-round-trips", test_camera_round_trips);
	g_test_add_func ("/editor/view-state/camera-matches-old-math", test_camera_matches_old_math);
	g_test_add_func ("/editor/view-state/axis-views", test_camera_axis_views);
	g_test_add_func ("/editor/view-state/camera-2d", test_camera_2d);
	g_test_add_func ("/editor/view-state/focus-frame", test_view_state_focus_frame);
	g_test_add_func ("/editor/view-state/settings", test_view_state_settings);

	return g_test_run ();
}
