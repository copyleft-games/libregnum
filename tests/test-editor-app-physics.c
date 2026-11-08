/* test-editor-app-physics.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Physics editing in levels (0.9.0 M5): the Physics panel (body, shapes,
 * joints, fit to mesh, one undo step per edit, grid gestures merged), the
 * physics.* IPC methods, the collider / joint / 2D collider gizmos and
 * their handle drags in the viewport, the Physics overlay (authoring paint
 * and the play world's debug draw), the inspector's Physics summary, the
 * hierarchy badge, play-mode live state and the level round trip. Every
 * test level is built in the test. GPU-only checks (a real render with
 * the overlay) run under the isolated Xvfb wrapper (xrun.sh).
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
static LrgEditorPhysicsPanel *
phys_panel (EditorAppFixture *f)
{
	LrgEditorPhysicsPanel *panel = lrg_editor_physics_panel_find (f->app);

	if (panel == NULL)
	{
		LrgDockPanel *dock = lrg_dock_panel_new (LRG_EDITOR_PHYSICS_PANEL_ID, "Physics");

		panel = lrg_editor_physics_panel_new (f->app);
		lrg_dock_panel_set_child (dock, LRG_WIDGET (panel));
		g_object_unref (panel);
		lrg_editor_app_add_panel (f->app, dock, "tab:inspector");
	}
	efx_frames (f, 2);
	return panel;
}

/* Brings the physics tab to the front and lays the window out. */
static void
present (EditorAppFixture *f)
{
	LrgDockArea  *dock = lrg_editor_app_get_dock (f->app);
	LrgDockPanel *p = lrg_dock_area_get_panel (dock, LRG_EDITOR_PHYSICS_PANEL_ID);

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

static guint
undo_count (EditorAppFixture *f)
{
	return lrg_editor_get_undo_count (f->editor);
}

/* A level with a ground plane and a cube 2 m up, the cube selected. */
static LrgNode *
crate_level (EditorAppFixture *f,
             LrgNode         **out_ground)
{
	LrgNode *ground, *cube;

	lrg_editor_new_level (f->editor, "phys");
	ground = create_node (f, "{'name':'Ground','kind':'primitive','primitive':'plane'}");
	cube = create_node (f, "{'name':'Crate','kind':'primitive','primitive':'cube'}");
	g_assert_nonnull (ground);
	g_assert_nonnull (cube);
	lrg_node_set_location_xyz (ground, 0.0f, 0.0f, 0.0f);
	lrg_node_set_scale_xyz (ground, 10.0f, 1.0f, 10.0f);
	lrg_node_set_location_xyz (cube, 0.0f, 2.0f, 0.0f);
	lrg_editor_select (f->editor, cube, FALSE);
	if (out_ground != NULL)
		*out_ground = ground;
	return cube;
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

static LrgNodePhysicsShape *
shape_of (LrgNode *node,
          guint    i)
{
	GPtrArray *shapes = lrg_node_physics_get_shapes (lrg_node_get_physics (node));

	g_assert_cmpuint (i, <, shapes->len);
	return g_ptr_array_index (shapes, i);
}

static LrgNodePhysicsJoint *
joint_of (LrgNode *node,
          guint    i)
{
	GPtrArray *joints = lrg_node_physics_get_joints (lrg_node_get_physics (node));

	g_assert_cmpuint (i, <, joints->len);
	return g_ptr_array_index (joints, i);
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

/* Frames the camera on the level from the front, a little above. */
static void
frame_front (EditorAppFixture *f)
{
	LrgEditorViewState  *vs = lrg_editor_app_get_view_state (f->app);
	LrgEditorViewCamera *cam = lrg_editor_view_state_get_camera (vs, 0);

	lrg_editor_view_camera_set_axis_view (cam, LRG_EDITOR_AXIS_VIEW_FRONT);
	lrg_editor_view_camera_frame_all (cam, lrg_editor_get_level (f->editor), 50.0f);
	lrg_editor_view_state_camera_changed (vs, 0);
	efx_frames (f, 2);
}

/* ==========================================================================
 * Panel: body
 * ========================================================================== */

/* The empty state adds a body with a fitted shape; undo / redo / remove. */
static void
test_body (EditorAppFixture *f,
           gconstpointer     data)
{
	LrgEditorPhysicsPanel *panel = phys_panel (f);
	LrgNode               *cube;
	gfloat                 he[3];
	guint                  undo;

	cube = crate_level (f, NULL);
	present (f);
	g_assert_true (lrg_editor_physics_panel_get_node (panel) == cube);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "physics-empty")));
	g_assert_false (lrg_widget_get_visible (efx_find (f, "physics-scroll")));

	/* the empty state's chip: dynamic body + box fitted to the unit cube */
	undo = undo_count (f);
	efx_click_widget (f, efx_find (f, "physics-empty-add"));
	g_assert_nonnull (lrg_node_get_physics (cube));
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	g_assert_cmpint (lrg_node_physics_get_body_kind (lrg_node_get_physics (cube)), ==, LRG_DYNAMICS_BODY_DYNAMIC);
	g_assert_cmpuint (lrg_node_physics_get_shapes (lrg_node_get_physics (cube))->len, ==, 1);
	g_assert_cmpint (lrg_node_physics_shape_get_kind (shape_of (cube, 0)), ==, LRG_COLLIDER3D_BOX);
	lrg_node_physics_shape_get_half_extents (shape_of (cube, 0), he);
	g_assert_cmpfloat_with_epsilon (he[0], 0.5f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (he[1], 0.5f, 1e-5f);
	g_assert_cmpint (lrg_editor_physics_panel_get_selected_shape (panel), ==, 0);
	efx_frames (f, 2);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "physics-scroll")));
	g_assert_true (lrg_widget_get_visible (LRG_WIDGET (lrg_editor_physics_panel_get_shape_grid (panel))));

	/* undo removes it, redo brings it back */
	lrg_editor_undo (f->editor);
	g_assert_null (lrg_node_get_physics (cube));
	lrg_editor_redo (f->editor);
	g_assert_nonnull (lrg_node_get_physics (cube));
	g_assert_false (lrg_editor_physics_panel_add_body (panel, NULL, LRG_DYNAMICS_BODY_STATIC, FALSE));

	/* the header's remove chip */
	efx_frames (f, 2);
	efx_click_widget (f, efx_find (f, "physics-remove-body"));
	g_assert_null (lrg_node_get_physics (cube));
	lrg_editor_undo (f->editor);
	g_assert_nonnull (lrg_node_get_physics (cube));

	/* a sphere primitive gets a sphere, a cylinder a cylinder */
	{
		LrgNode *ball = create_node (f, "{'name':'Ball','kind':'primitive','primitive':'uv-sphere'}");
		LrgNode *can = create_node (f, "{'name':'Can','kind':'primitive','primitive':'cylinder'}");

		g_assert_true (lrg_editor_physics_panel_add_body (panel, ball, LRG_DYNAMICS_BODY_DYNAMIC, TRUE));
		g_assert_cmpint (lrg_node_physics_shape_get_kind (shape_of (ball, 0)), ==, LRG_COLLIDER3D_SPHERE);
		g_assert_cmpfloat_with_epsilon (lrg_node_physics_shape_get_radius (shape_of (ball, 0)), 0.5f, 1e-5f);
		g_assert_true (lrg_editor_physics_panel_add_body (panel, can, LRG_DYNAMICS_BODY_KINEMATIC, TRUE));
		g_assert_cmpint (lrg_node_physics_shape_get_kind (shape_of (can, 0)), ==, LRG_COLLIDER3D_CYLINDER);
		g_assert_cmpint (lrg_node_physics_get_body_kind (lrg_node_get_physics (can)), ==,
		                 LRG_DYNAMICS_BODY_KINEMATIC);
	}

	/* nothing selected: the panel explains */
	lrg_editor_select (f->editor, NULL, FALSE);
	efx_frames (f, 2);
	g_assert_null (lrg_editor_physics_panel_get_node (panel));
	g_assert_true (lrg_widget_get_visible (efx_find (f, "physics-hint-none")));
}

/* Field edits: one step each; a grid gesture is one step; enums by nick. */
static void
test_fields (EditorAppFixture *f,
             gconstpointer     data)
{
	LrgEditorPhysicsPanel *panel = phys_panel (f);
	LrgPropertyGrid       *grid;
	LrgPropertySource     *src;
	LrgNode               *cube;
	LrgNodePhysics        *before;
	g_auto(GValue)         v = G_VALUE_INIT;
	g_auto(GValue)         k = G_VALUE_INIT;
	g_auto(GValue)         l = G_VALUE_INIT;
	g_auto(GValue)         out = G_VALUE_INIT;
	guint                  undo;
	gint                   i;

	cube = crate_level (f, NULL);
	efx_frames (f, 2);
	g_assert_true (lrg_editor_physics_panel_add_body (panel, NULL, LRG_DYNAMICS_BODY_DYNAMIC, TRUE));
	before = g_object_ref (lrg_node_get_physics (cube));

	/* mass: one step, on a copy (the old block is untouched) */
	undo = undo_count (f);
	gv_double (&v, 20.0);
	g_assert_true (lrg_editor_physics_panel_set_body_field (panel, NULL, "mass", &v));
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	g_assert_cmpfloat_with_epsilon (lrg_node_physics_get_mass (lrg_node_get_physics (cube)), 20.0f, 1e-4f);
	g_assert_cmpfloat (lrg_node_physics_get_mass (before), !=, 20.0f);
	g_object_unref (before);
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat (lrg_node_physics_get_mass (lrg_node_get_physics (cube)), !=, 20.0f);
	lrg_editor_redo (f->editor);

	/* enums by nick, '_' names, layers */
	gv_string (&k, "static");
	g_assert_true (lrg_editor_physics_panel_set_body_field (panel, NULL, "body_kind", &k));
	g_assert_cmpint (lrg_node_physics_get_body_kind (lrg_node_get_physics (cube)), ==, LRG_DYNAMICS_BODY_STATIC);
	gv_string (&l, "debris");
	g_assert_true (lrg_editor_physics_panel_set_body_field (panel, NULL, "layer", &l));
	g_assert_cmpstr (lrg_node_physics_get_layer (lrg_node_get_physics (cube)), ==, "debris");
	g_value_set_string (&k, "no-such-kind");
	g_assert_false (lrg_editor_physics_panel_set_body_field (panel, NULL, "body-kind", &k));
	g_assert_false (lrg_editor_physics_panel_set_body_field (panel, NULL, "no-such-field", &v));

	/* the body grid lists the fields and reads the live block */
	efx_frames (f, 2);
	grid = lrg_editor_physics_panel_get_body_grid (panel);
	g_assert_cmpuint (lrg_property_grid_get_n_sources (grid), ==, 1);
	src = lrg_property_grid_get_source (grid, 0);
	g_assert_nonnull (lrg_property_source_find_property (src, "mass"));
	g_assert_nonnull (lrg_property_source_find_property (src, "layer"));
	g_assert_nonnull (lrg_property_grid_get_row (grid, "gravity-factor"));
	g_assert_true (lrg_property_source_get_value (src, "mass", &out));
	g_assert_cmpfloat_with_epsilon (g_value_get_float (&out), 20.0f, 1e-4f);

	/* a grid gesture (a drag): many values, one step */
	undo = undo_count (f);
	g_signal_emit_by_name (grid, "edit-begin", "mass", 7u);
	for (i = 1; i <= 5; i++)
	{
		g_auto(GValue) m = G_VALUE_INIT;

		g_value_init (&m, G_TYPE_FLOAT);
		g_value_set_float (&m, 20.0f + (gfloat) i);
		g_assert_true (lrg_property_source_set_value (src, "mass", &m));
	}
	g_signal_emit_by_name (grid, "edit-end", "mass", 7u, TRUE);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	g_assert_cmpfloat_with_epsilon (lrg_node_physics_get_mass (lrg_node_get_physics (cube)), 25.0f, 1e-4f);
	lrg_editor_undo (f->editor);
	g_assert_cmpfloat_with_epsilon (lrg_node_physics_get_mass (lrg_node_get_physics (cube)), 20.0f, 1e-4f);

	/* a second gesture on the same field is a step of its own */
	g_signal_emit_by_name (grid, "edit-begin", "mass", 8u);
	{
		g_auto(GValue) m = G_VALUE_INIT;

		g_value_init (&m, G_TYPE_FLOAT);
		g_value_set_float (&m, 30.0f);
		g_assert_true (lrg_property_source_set_value (src, "mass", &m));
	}
	g_signal_emit_by_name (grid, "edit-end", "mass", 8u, TRUE);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
}

/* ==========================================================================
 * Panel: shapes and joints
 * ========================================================================== */

static void
test_shapes (EditorAppFixture *f,
             gconstpointer     data)
{
	LrgEditorPhysicsPanel *panel = phys_panel (f);
	LrgPropertyGrid       *grid;
	LrgNode               *cube;
	g_auto(GValue)         vec = G_VALUE_INIT;
	g_auto(GValue)         t = G_VALUE_INIT;
	gfloat                 he[3], off[3];
	gfloat                 x, y, w, h;
	guint                  undo;

	cube = crate_level (f, NULL);
	present (f);
	g_assert_true (lrg_editor_physics_panel_add_body (panel, NULL, LRG_DYNAMICS_BODY_DYNAMIC, TRUE));

	/* add: a sphere trigger and a capsule; the new one is selected */
	g_assert_cmpint (lrg_editor_physics_panel_add_shape (panel, NULL, LRG_COLLIDER3D_SPHERE, FALSE), ==, 1);
	g_assert_cmpint (lrg_editor_physics_panel_get_selected_shape (panel), ==, 1);
	g_assert_cmpint (lrg_editor_physics_panel_add_shape (panel, NULL, LRG_COLLIDER3D_CAPSULE, TRUE), ==, 2);
	g_assert_cmpfloat_with_epsilon (lrg_node_physics_shape_get_radius (shape_of (cube, 2)), 0.5f, 1e-5f);
	g_assert_cmpfloat_with_epsilon (lrg_node_physics_shape_get_half_height (shape_of (cube, 2)), 0.0f, 1e-5f);

	/* fields: a vector, a bool, the shape grid's rows follow the kind */
	undo = undo_count (f);
	g_value_init (&vec, GRL_TYPE_VECTOR3);
	g_value_take_boxed (&vec, grl_vector3_new (0.0f, 1.0f, 0.0f));
	g_assert_true (lrg_editor_physics_panel_set_shape_field (panel, NULL, 1, "offset", &vec));
	g_value_init (&t, G_TYPE_BOOLEAN);
	g_value_set_boolean (&t, TRUE);
	g_assert_true (lrg_editor_physics_panel_set_shape_field (panel, NULL, 1, "trigger", &t));
	g_assert_cmpuint (undo_count (f), ==, undo + 2);
	lrg_node_physics_shape_get_offset (shape_of (cube, 1), off);
	g_assert_cmpfloat_with_epsilon (off[1], 1.0f, 1e-6f);
	g_assert_true (lrg_node_physics_shape_get_trigger (shape_of (cube, 1)));

	lrg_editor_physics_panel_set_selected_shape (panel, 1);
	efx_frames (f, 2);
	grid = lrg_editor_physics_panel_get_shape_grid (panel);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "radius"));
	g_assert_null (lrg_property_grid_get_row (grid, "half-extents"));
	lrg_editor_physics_panel_set_selected_shape (panel, 0);
	efx_frames (f, 2);
	g_assert_nonnull (lrg_property_grid_get_row (grid, "half-extents"));
	g_assert_null (lrg_property_grid_get_row (grid, "radius"));
	lrg_editor_physics_panel_set_selected_shape (panel, 99);
	g_assert_cmpint (lrg_editor_physics_panel_get_selected_shape (panel), ==, -1);

	/* reorder: the sphere moves to the front */
	g_assert_true (lrg_editor_physics_panel_move_shape (panel, NULL, 1, 0));
	g_assert_cmpint (lrg_node_physics_shape_get_kind (shape_of (cube, 0)), ==, LRG_COLLIDER3D_SPHERE);
	g_assert_cmpint (lrg_node_physics_shape_get_kind (shape_of (cube, 1)), ==, LRG_COLLIDER3D_BOX);
	g_assert_false (lrg_editor_physics_panel_move_shape (panel, NULL, 0, 0));
	lrg_editor_undo (f->editor);
	g_assert_cmpint (lrg_node_physics_shape_get_kind (shape_of (cube, 0)), ==, LRG_COLLIDER3D_BOX);

	/* fit: shape 0 becomes a sphere of the cube's half size */
	g_assert_true (lrg_editor_physics_panel_fit_shape (panel, NULL, 0, LRG_COLLIDER3D_SPHERE));
	g_assert_cmpint (lrg_node_physics_shape_get_kind (shape_of (cube, 0)), ==, LRG_COLLIDER3D_SPHERE);
	g_assert_cmpfloat_with_epsilon (lrg_node_physics_shape_get_radius (shape_of (cube, 0)), 0.5f, 1e-5f);
	lrg_editor_undo (f->editor);

	/* the pure fit helper: an off-centre box */
	{
		g_autoptr(LrgNodePhysicsShape) s = lrg_node_physics_shape_new (LRG_COLLIDER3D_BOX);
		const gfloat                   mn[3] = { -1.0f, 0.0f, -0.5f };
		const gfloat                   mx[3] = { 1.0f, 3.0f, 0.5f };

		lrg_editor_physics_fit_shape_to_bounds (s, LRG_COLLIDER3D_BOX, mn, mx);
		lrg_node_physics_shape_get_half_extents (s, he);
		lrg_node_physics_shape_get_offset (s, off);
		g_assert_cmpfloat_with_epsilon (he[1], 1.5f, 1e-6f);
		g_assert_cmpfloat_with_epsilon (off[1], 1.5f, 1e-6f);
		lrg_editor_physics_fit_shape_to_bounds (s, LRG_COLLIDER3D_CAPSULE, mn, mx);
		g_assert_cmpfloat_with_epsilon (lrg_node_physics_shape_get_radius (s), 1.0f, 1e-6f);
		g_assert_cmpfloat_with_epsilon (lrg_node_physics_shape_get_half_height (s), 0.5f, 1e-6f);
		lrg_editor_physics_fit_shape_to_bounds (s, LRG_COLLIDER3D_CONVEX, mn, mx);
		lrg_node_physics_shape_get_offset (s, off);
		g_assert_cmpfloat (off[1], ==, 0.0f);
	}

	/* the list: click the second row to select it, then its remove button.
	 * The list starts below the fold of the inspector split. */
	present (f);
	lrg_scroll_box_scroll_to_widget (LRG_SCROLL_BOX (efx_find (f, "physics-scroll")),
	                                 efx_find (f, "physics-shape-list"));
	efx_frames (f, 40);
	efx_rect_of (efx_find (f, "physics-shape-list"), &x, &y, &w, &h);
	g_assert_cmpfloat (h, >=, 3.0f * 24.0f);
	efx_click (f, x + 40.0f, y + 1.0f + 24.0f * 1.5f);
	g_assert_cmpint (lrg_editor_physics_panel_get_selected_shape (panel), ==, 1);
	undo = undo_count (f);
	efx_click (f, x + w - 2.0f - 11.0f, y + 1.0f + 24.0f * 1.5f);
	g_assert_cmpuint (lrg_node_physics_get_shapes (lrg_node_get_physics (cube))->len, ==, 2);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	/* row 0's "down" button swaps the remaining two */
	efx_click (f, x + w - 2.0f - 22.0f - 11.0f, y + 1.0f + 12.0f);
	g_assert_cmpint (lrg_node_physics_shape_get_kind (shape_of (cube, 1)), ==, LRG_COLLIDER3D_BOX);

	/* removing all shapes keeps the body */
	g_assert_true (lrg_editor_physics_panel_remove_shape (panel, NULL, 0));
	g_assert_true (lrg_editor_physics_panel_remove_shape (panel, NULL, 0));
	g_assert_false (lrg_editor_physics_panel_remove_shape (panel, NULL, 0));
	g_assert_nonnull (lrg_node_get_physics (cube));
	g_assert_cmpint (lrg_editor_physics_panel_get_selected_shape (panel), ==, -1);
}

static void
test_joints (EditorAppFixture *f,
             gconstpointer     data)
{
	LrgEditorPhysicsPanel *panel = phys_panel (f);
	LrgPropertySource     *src;
	LrgNode               *cube, *ground;
	g_auto(GValue)         s = G_VALUE_INIT;
	g_auto(GValue)         out = G_VALUE_INIT;
	g_auto(GValue)         ax = G_VALUE_INIT;
	gfloat                 axis[3];
	guint                  undo;

	cube = crate_level (f, &ground);
	efx_frames (f, 2);

	/* a hinge to the ground (by name path) on a node without a body yet */
	undo = undo_count (f);
	g_assert_cmpint (lrg_editor_physics_panel_add_joint (panel, NULL, LRG_JOINT_HINGE, "Ground"), ==, 0);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	g_assert_nonnull (lrg_node_get_physics (cube));
	g_assert_cmpstr (lrg_node_physics_joint_get_target (joint_of (cube, 0)), ==, lrg_node_get_guid (ground));
	g_assert_cmpint (lrg_editor_physics_panel_get_selected_joint (panel), ==, 0);
	g_assert_cmpint (lrg_editor_physics_panel_add_joint (panel, NULL, LRG_JOINT_POINT, "Nope"), ==, -1);
	g_assert_cmpint (lrg_editor_physics_panel_add_joint (panel, NULL, LRG_JOINT_POINT, "Crate"), ==, -1);

	/* the grid shows the target as a path; writing a path stores a GUID */
	efx_frames (f, 2);
	src = lrg_property_grid_get_source (lrg_editor_physics_panel_get_joint_grid (panel), 0);
	g_assert_true (lrg_property_source_get_value (src, "target", &out));
	g_assert_cmpstr (g_value_get_string (&out), ==, "Ground");
	gv_string (&s, "");
	g_assert_true (lrg_editor_physics_panel_set_joint_field (panel, NULL, 0, "target", &s));
	g_assert_null (lrg_node_physics_joint_get_target (joint_of (cube, 0)));
	g_value_set_string (&s, lrg_node_get_guid (ground));
	g_assert_true (lrg_editor_physics_panel_set_joint_field (panel, NULL, 0, "target", &s));
	g_assert_cmpstr (lrg_node_physics_joint_get_target (joint_of (cube, 0)), ==, lrg_node_get_guid (ground));

	/* axis as "x,y,z", limits */
	gv_string (&ax, "0, 0, 2");
	g_assert_true (lrg_editor_physics_panel_set_joint_field (panel, NULL, 0, "axis", &ax));
	lrg_node_physics_joint_get_axis (joint_of (cube, 0), axis);
	g_assert_cmpfloat_with_epsilon (axis[2], 1.0f, 1e-6f);
	g_value_set_string (&ax, "0,0,0");
	g_assert_false (lrg_editor_physics_panel_set_joint_field (panel, NULL, 0, "axis", &ax));
	{
		g_auto(GValue) on = G_VALUE_INIT;
		g_auto(GValue) mx = G_VALUE_INIT;
		gfloat         lo, hi;

		g_value_init (&on, G_TYPE_BOOLEAN);
		g_value_set_boolean (&on, TRUE);
		g_assert_true (lrg_editor_physics_panel_set_joint_field (panel, NULL, 0, "limits-enabled", &on));
		gv_double (&mx, 1.2);
		g_assert_true (lrg_editor_physics_panel_set_joint_field (panel, NULL, 0, "limit-max", &mx));
		g_assert_true (lrg_node_physics_joint_get_limits (joint_of (cube, 0), &lo, &hi));
		g_assert_cmpfloat_with_epsilon (hi, 1.2f, 1e-5f);
	}

	/* remove, undo */
	g_assert_true (lrg_editor_physics_panel_remove_joint (panel, NULL, 0));
	g_assert_cmpuint (lrg_node_physics_get_joints (lrg_node_get_physics (cube))->len, ==, 0);
	g_assert_cmpint (lrg_editor_physics_panel_get_selected_joint (panel), ==, -1);
	lrg_editor_undo (f->editor);
	g_assert_cmpuint (lrg_node_physics_get_joints (lrg_node_get_physics (cube))->len, ==, 1);
}

/* ==========================================================================
 * IPC
 * ========================================================================== */

static JsonObject *
res_obj (JsonNode *r)
{
	return json_node_get_object (r);
}

static void
test_ipc (EditorAppFixture *f,
          gconstpointer     data)
{
	LrgNode          *cube, *ground;
	g_autofree gchar *req = NULL;
	g_autofree gchar *err = NULL;
	guint             undo;

	phys_panel (f);
	cube = crate_level (f, &ground);
	efx_frames (f, 2);

	/* get: no body */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.get','params':{'node':'Crate'}}");

		g_assert_true (json_object_get_null_member (res_obj (r), "body") ||
		               json_node_is_null (json_object_get_member (res_obj (r), "body")));
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (res_obj (r), "layers")), >, 0);
	}

	/* add_body (static, no shape) */
	undo = undo_count (f);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.add_body','params':{'kind':'static',"
		                                           "'shape':'none'}}");
		JsonObject         *b = json_object_get_object_member (res_obj (r), "body");

		g_assert_cmpstr (json_object_get_string_member (b, "body-kind"), ==, "static");
		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (res_obj (r), "shapes")), ==, 0);
	}
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	err = efx_ipc (f, "{'id':1,'method':'physics.add_body'}");
	g_assert_nonnull (strstr (err, "error"));
	g_clear_pointer (&err, g_free);

	/* set_body: several fields, one step */
	undo = undo_count (f);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.set_body','params':{'values':"
		                                           "{'body_kind':'dynamic','mass':12.5,'ccd':true}}}");
		JsonObject         *b = json_object_get_object_member (res_obj (r), "body");

		g_assert_cmpstr (json_object_get_string_member (b, "body-kind"), ==, "dynamic");
		g_assert_cmpfloat_with_epsilon (json_object_get_double_member (b, "mass"), 12.5, 1e-4);
		g_assert_true (json_object_get_boolean_member (b, "ccd"));
	}
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	err = efx_ipc (f, "{'id':1,'method':'physics.set_body','params':{'values':{'mass':1,'bogus':2}}}");
	g_assert_nonnull (strstr (err, "bogus"));
	g_clear_pointer (&err, g_free);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);

	/* add_shape with values (one step), set_shape, move_shape */
	undo = undo_count (f);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.add_shape','params':{'kind':'box',"
		                                           "'values':{'half-extents':[1,0.25,2],'trigger':true}}}");

		g_assert_cmpint (json_object_get_int_member (res_obj (r), "index"), ==, 0);
	}
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	g_assert_true (lrg_node_physics_shape_get_trigger (shape_of (cube, 0)));
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.add_shape','params':{'kind':'sphere',"
		                                           "'fit':true}}");
		JsonArray          *shapes = json_object_get_array_member (res_obj (r), "shapes");

		g_assert_cmpuint (json_array_get_length (shapes), ==, 2);
		g_assert_cmpfloat_with_epsilon (json_object_get_double_member (json_array_get_object_element (shapes, 1),
		                                                               "radius"), 0.5, 1e-5);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.set_shape','params':{'index':1,"
		                                           "'values':{'radius':0.75,'offset':[0,1,0],'layer':'debris'}}}");
		JsonObject         *s = json_array_get_object_element (json_object_get_array_member (res_obj (r), "shapes"), 1);

		g_assert_cmpfloat_with_epsilon (json_object_get_double_member (s, "radius"), 0.75, 1e-5);
		g_assert_cmpstr (json_object_get_string_member (s, "layer"), ==, "debris");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.move_shape','params':{'index':1,'to':0}}");
		JsonObject         *s = json_array_get_object_element (json_object_get_array_member (res_obj (r), "shapes"), 0);

		g_assert_cmpstr (json_object_get_string_member (s, "kind"), ==, "sphere");
	}
	err = efx_ipc (f, "{'id':1,'method':'physics.set_shape','params':{'index':9,'values':{'radius':1}}}");
	g_assert_nonnull (strstr (err, "error"));
	g_clear_pointer (&err, g_free);

	/* fit shape 1 (the box) as a capsule */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.fit','params':{'index':1,"
		                                           "'kind':'capsule'}}");
		JsonObject         *s = json_array_get_object_element (json_object_get_array_member (res_obj (r), "shapes"), 1);

		g_assert_cmpstr (json_object_get_string_member (s, "kind"), ==, "capsule");
	}

	/* joints */
	req = g_strdup_printf ("{'id':1,'method':'physics.add_joint','params':{'kind':'hinge','target':'%s',"
	                       "'values':{'limits-enabled':true,'limit-min':-0.5,'limit-max':0.5,'axis':[0,0,1]}}}",
	                       lrg_node_get_guid (ground));
	undo = undo_count (f);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, req);
		JsonObject         *j = json_array_get_object_element (json_object_get_array_member (res_obj (r), "joints"), 0);

		g_assert_cmpstr (json_object_get_string_member (j, "kind"), ==, "hinge");
		g_assert_cmpstr (json_object_get_string_member (j, "target-name"), ==, "Ground");
		g_assert_true (json_object_get_boolean_member (j, "limits-enabled"));
		g_assert_cmpfloat_with_epsilon (json_object_get_double_member (j, "limit-max"), 0.5, 1e-5);
	}
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.set_joint','params':{'index':0,"
		                                           "'values':{'target':'','break-force':500}}}");
		JsonObject         *j = json_array_get_object_element (json_object_get_array_member (res_obj (r), "joints"), 0);

		g_assert_true (json_node_is_null (json_object_get_member (j, "target")));
		g_assert_cmpfloat_with_epsilon (json_object_get_double_member (j, "break-force"), 500.0, 1e-3);
	}
	err = efx_ipc (f, "{'id':1,'method':'physics.add_joint','params':{'kind':'hinge','target':'Missing'}}");
	g_assert_nonnull (strstr (err, "error"));
	g_clear_pointer (&err, g_free);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.remove_joint','params':{'index':0}}");

		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (res_obj (r), "joints")), ==, 0);
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.remove_shape','params':{'index':0}}");

		g_assert_cmpuint (json_array_get_length (json_object_get_array_member (res_obj (r), "shapes")), ==, 1);
	}

	/* state while authoring; show; remove_body */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.state'}");

		g_assert_false (json_object_get_boolean_member (res_obj (r), "playing"));
		g_assert_true (json_object_get_boolean_member (res_obj (r), "has-body"));
		g_assert_true (json_node_is_null (json_object_get_member (res_obj (r), "body")));
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.show'}");

		g_assert_cmpstr (json_object_get_string_member (res_obj (r), "panel"), ==, "physics");
	}
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.remove_body','params':{'node':'Crate'}}");

		g_assert_true (json_node_is_null (json_object_get_member (res_obj (r), "body")));
	}
	g_assert_null (lrg_node_get_physics (cube));
	err = efx_ipc (f, "{'id':1,'method':'physics.get','params':{'node':'Nope'}}");
	g_assert_nonnull (strstr (err, "No node"));
}

/* ==========================================================================
 * Gizmos
 * ========================================================================== */

/* Handles: positions, hit test, drag values; a drag on the canvas is one
 * step, Escape cancels; joint anchors; 2D colliders; painting. */
static void
test_gizmo (EditorAppFixture *f,
            gconstpointer     data)
{
	g_autoptr(LrgWidget)   old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewportPanel *vp = lrg_editor_viewport_split_get_panel (split, 0);
	LrgEditorPhysicsPanel  *panel = phys_panel (f);
	LrgEditorViewState     *vs = lrg_editor_app_get_view_state (f->app);
	LrgEditorView           view;
	LrgNode                *cube;
	gfloat                  p[3], u, v, x, y, he[3], val[3];
	guint                   index, undo;

	cube = crate_level (f, NULL);
	lrg_node_set_location_xyz (cube, 0.0f, 0.0f, 0.0f);
	lrg_node_set_scale_xyz (cube, 2.0f, 1.0f, 1.0f);
	g_assert_true (lrg_editor_physics_panel_add_body (panel, NULL, LRG_DYNAMICS_BODY_DYNAMIC, TRUE));
	g_assert_true (lrg_editor_physics_has_gizmo (cube));
	frame_front (f);
	lrg_editor_view_state_get_view (vs, 0, &view);

	/* +X face handle: half extent 0.5 times the node's X scale 2 */
	g_assert_true (lrg_editor_physics_get_handle_position (&view, cube, LRG_EDITOR_PHYSICS_HANDLE_HALF_X, 0, p));
	g_assert_cmpfloat_with_epsilon (p[0], 1.0f, 1e-4f);
	g_assert_false (lrg_editor_physics_get_handle_position (&view, cube, LRG_EDITOR_PHYSICS_HANDLE_RADIUS, 0, p));
	g_assert_true (lrg_editor_physics_get_handle_position (&view, cube, LRG_EDITOR_PHYSICS_HANDLE_HALF_X, 0, p));
	g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
	g_assert_cmpint (lrg_editor_physics_hit_test (&view, cube, u, v, &index), ==, LRG_EDITOR_PHYSICS_HANDLE_HALF_X);
	g_assert_cmpuint (index, ==, 0);
	g_assert_true (lrg_editor_physics_drag_value (&view, cube, LRG_EDITOR_PHYSICS_HANDLE_HALF_X, 0, u, v, val));
	g_assert_cmpfloat_with_epsilon (val[0], 0.5f, 0.02f);
	g_assert_cmpstr (lrg_editor_physics_handle_get_field (LRG_EDITOR_PHYSICS_HANDLE_HALF_X), ==, "half-extent-x");

	/* drag it outwards on the canvas: one step; undo restores */
	undo = undo_count (f);
	vp_to_canvas (f, u, v, &x, &y);
	efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x + 60.0f, y, 4);
	lrg_node_physics_shape_get_half_extents (shape_of (cube, 0), he);
	g_assert_cmpfloat (he[0], >, 0.6f);
	g_assert_cmpfloat_with_epsilon (he[1], 0.5f, 1e-5f);
	g_assert_cmpuint (undo_count (f), ==, undo + 1);
	lrg_editor_undo (f->editor);
	lrg_node_physics_shape_get_half_extents (shape_of (cube, 0), he);
	g_assert_cmpfloat_with_epsilon (he[0], 0.5f, 1e-5f);

	/* Escape during a drag puts the old size back, no step */
	efx_move (f, x, y);
	efx_button (f, x, y, GRL_MOUSE_BUTTON_LEFT, TRUE);
	efx_move (f, x + 50.0f, y);
	g_assert_cmpint (lrg_editor_viewport_panel_get_physics_handle (vp, NULL), ==, LRG_EDITOR_PHYSICS_HANDLE_HALF_X);
	efx_key (f, GRL_KEY_ESCAPE, LRG_INPUT_MODIFIER_NONE);
	efx_button (f, x + 50.0f, y, GRL_MOUSE_BUTTON_LEFT, FALSE);
	lrg_node_physics_shape_get_half_extents (shape_of (cube, 0), he);
	g_assert_cmpfloat_with_epsilon (he[0], 0.5f, 1e-5f);
	g_assert_cmpuint (undo_count (f), ==, undo);

	/* a capsule: radius on +X, half height on the cap tip */
	g_assert_true (lrg_editor_physics_panel_fit_shape (panel, NULL, 0, LRG_COLLIDER3D_CAPSULE));
	{
		g_auto(GValue) hh = G_VALUE_INIT;

		gv_double (&hh, 1.0);
		g_assert_true (lrg_editor_physics_panel_set_shape_field (panel, NULL, 0, "half-height", &hh));
	}
	g_assert_true (lrg_editor_physics_get_handle_position (&view, cube, LRG_EDITOR_PHYSICS_HANDLE_HALF_HEIGHT, 0, p));
	/* radius 0.5 * max(2, 1) = 1, half height 1: the tip at y = 2 */
	g_assert_cmpfloat_with_epsilon (p[1], 2.0f, 1e-4f);
	g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
	g_assert_true (lrg_editor_physics_drag_value (&view, cube, LRG_EDITOR_PHYSICS_HANDLE_HALF_HEIGHT, 0, u, v, val));
	g_assert_cmpfloat_with_epsilon (val[0], 1.0f, 0.03f);

	/* a joint anchor: dragged on the camera plane, in node space */
	g_assert_cmpint (lrg_editor_physics_panel_add_joint (panel, NULL, LRG_JOINT_HINGE, NULL), ==, 0);
	g_assert_true (lrg_editor_physics_get_handle_position (&view, cube, LRG_EDITOR_PHYSICS_HANDLE_ANCHOR, 0, p));
	g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
	g_assert_true (lrg_editor_physics_drag_value (&view, cube, LRG_EDITOR_PHYSICS_HANDLE_ANCHOR, 0, u, v, val));
	g_assert_cmpfloat_with_epsilon (val[0], 0.0f, 0.02f);
	g_assert_cmpfloat_with_epsilon (val[1], 0.0f, 0.02f);

	/* the overlay paints the gizmo (and counts it) without a GPU */
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
	g_assert_cmpuint (lrg_editor_viewport_panel_get_physics_painted (vp), ==, 2);

	/* paint functions straight into a list */
	{
		g_autoptr(LrgPaintList) list = lrg_paint_list_new ();

		g_assert_cmpuint (lrg_editor_gizmo_paint_physics_shapes (list, &view, lrg_editor_get_level (f->editor), cube,
		                                                         LRG_DYNAMICS_DEBUG_ALL, 1.0f, 0.0f, 0.0f, 1.0f),
		                  ==, 2);
		g_assert_cmpuint (lrg_paint_list_get_n_instances (list), >, 20);
	}

	/* a 2D collider description: box handles on its edges */
	{
		g_autoptr(LrgNode)          node2d = lrg_node_new ("Sprite2D");
		g_autoptr(LrgComponentDesc) col = lrg_component_desc_new ("collider");

		lrg_node_set_is_2d (node2d, TRUE);
		lrg_component_desc_set_string (col, "shape", "box");
		lrg_component_desc_set_double (col, "width", 2.0);
		lrg_component_desc_set_double (col, "height", 1.0);
		lrg_node_add_component (node2d, col);
		lrg_editor_add_node (f->editor, node2d, NULL);
		g_assert_true (lrg_editor_physics_has_gizmo (node2d));
		g_assert_true (lrg_editor_physics_get_handle_position (&view, node2d, LRG_EDITOR_PHYSICS_HANDLE_WIDTH_2D, 0, p));
		g_assert_cmpfloat_with_epsilon (p[0], 1.0f, 1e-4f);
		g_assert_true (lrg_editor_physics_get_handle_position (&view, node2d, LRG_EDITOR_PHYSICS_HANDLE_HEIGHT_2D, 0,
		                                                       p));
		g_assert_cmpfloat_with_epsilon (p[1], 0.5f, 1e-4f);
		g_assert_false (lrg_editor_physics_get_handle_position (&view, node2d, LRG_EDITOR_PHYSICS_HANDLE_RADIUS_2D, 0,
		                                                        p));
		g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
		g_assert_true (lrg_editor_physics_drag_value (&view, node2d, LRG_EDITOR_PHYSICS_HANDLE_HEIGHT_2D, 0, u, v, val));
		g_assert_cmpfloat_with_epsilon (val[0], 1.0f, 0.03f);

		/* drag the width handle on the canvas: the desc's width, one step */
		lrg_editor_select (f->editor, node2d, FALSE);
		efx_frames (f, 2);
		g_assert_true (lrg_editor_physics_get_handle_position (&view, node2d, LRG_EDITOR_PHYSICS_HANDLE_WIDTH_2D, 0, p));
		g_assert_true (lrg_editor_view_project (&view, p, &u, &v, NULL));
		vp_to_canvas (f, u, v, &x, &y);
		undo = undo_count (f);
		efx_drag (f, GRL_MOUSE_BUTTON_LEFT, x, y, x + 40.0f, y, 4);
		g_assert_cmpuint (undo_count (f), ==, undo + 1);
		{
			const GValue  *w = lrg_component_desc_get_value (lrg_node_find_component (node2d, "collider"), "width");
			g_auto(GValue) d = G_VALUE_INIT;

			g_value_init (&d, G_TYPE_DOUBLE);
			g_assert_true (g_value_transform (w, &d));
			g_assert_cmpfloat (g_value_get_double (&d), >, 2.1);
		}
	}
}

/* ==========================================================================
 * Overlay, inspector, hierarchy
 * ========================================================================== */

static void
test_overlay (EditorAppFixture *f,
              gconstpointer     data)
{
	g_autoptr(LrgWidget)   old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewportPanel *vp = lrg_editor_viewport_split_get_panel (split, 0);
	LrgEditorPhysicsPanel  *panel = phys_panel (f);
	LrgNode                *cube, *ground;
	g_autofree gchar       *err = NULL;

	cube = crate_level (f, &ground);
	g_assert_true (lrg_editor_physics_panel_add_body (panel, cube, LRG_DYNAMICS_BODY_DYNAMIC, TRUE));
	g_assert_true (lrg_editor_physics_panel_add_body (panel, ground, LRG_DYNAMICS_BODY_STATIC, TRUE));
	frame_front (f);
	g_assert_cmpuint (lrg_editor_viewport_panel_get_physics_overlay (vp), ==, 0);

	/* IPC: replace, enable, disable, names */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.overlay','params':{'flags':['shapes',"
		                                           "'joints']}}");
		JsonArray          *a = json_object_get_array_member (json_node_get_object (r), "flags");

		g_assert_cmpuint (json_array_get_length (a), ==, 2);
	}
	g_assert_cmpuint (lrg_editor_viewport_panel_get_physics_overlay (vp), ==,
	                  LRG_DYNAMICS_DEBUG_SHAPES | LRG_DYNAMICS_DEBUG_CONSTRAINTS);
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.overlay','params':{'enable':['aabbs',"
		                                           "'com'],'disable':['joints']}}");
	}
	g_assert_cmpuint (lrg_editor_viewport_panel_get_physics_overlay (vp), ==,
	                  LRG_DYNAMICS_DEBUG_SHAPES | LRG_DYNAMICS_DEBUG_AABBS | LRG_DYNAMICS_DEBUG_CENTER_OF_MASS);
	err = efx_ipc (f, "{'id':1,'method':'physics.overlay','params':{'flags':['wobbly']}}");
	g_assert_nonnull (strstr (err, "wobbly"));

	/* the authoring overlay paints both bodies (the selected one as its
	 * gizmo); without a selection both come from the overlay */
	lrg_editor_select (f->editor, NULL, FALSE);
	efx_frames (f, 2);
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
	g_assert_cmpuint (lrg_editor_viewport_panel_get_physics_painted (vp), ==, 2);
	lrg_editor_viewport_panel_set_physics_overlay (vp, 0);
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
	g_assert_cmpuint (lrg_editor_viewport_panel_get_physics_painted (vp), ==, 0);

	/* the menu's actions toggle single flags */
	{
		GActionGroup *group = lrg_widget_get_action_group (LRG_WIDGET (vp), "vp");

		g_assert_nonnull (group);
		g_action_group_activate_action (group, "physics-contacts", NULL);
		g_assert_cmpuint (lrg_editor_viewport_panel_get_physics_overlay (vp), ==, LRG_DYNAMICS_DEBUG_CONTACTS);
		g_assert_true (g_variant_get_boolean (g_action_group_get_action_state (group, "physics-contacts")));
		g_action_group_activate_action (group, "physics-contacts", NULL);
		g_assert_cmpuint (lrg_editor_viewport_panel_get_physics_overlay (vp), ==, 0);
	}

	/* nothing to record while authoring */
	lrg_editor_viewport_panel_set_physics_overlay (vp, LRG_DYNAMICS_DEBUG_ALL);
	g_assert_cmpuint (lrg_editor_viewport_panel_record_physics (vp), ==, 0);
	g_assert_nonnull (lrg_editor_viewport_panel_get_physics_debug_draw (vp));
}

static void
test_inspector_hierarchy (EditorAppFixture *f,
                          gconstpointer     data)
{
	LrgEditorPhysicsPanel *panel = phys_panel (f);
	LrgNode               *cube;
	LrgWidget             *section, *summary;
	JsonArray             *rows;
	guint                  i, found = 0;

	cube = crate_level (f, NULL);
	efx_frames (f, 3);

	/* the inspector's Physics section: no body -> "Add Body" */
	section = efx_find (f, "inspector-section-physics");
	g_assert_nonnull (section);
	g_assert_true (lrg_widget_get_visible (section));
	summary = efx_find (f, "inspector-physics-summary");
	g_assert_cmpstr (lrg_label_get_text (LRG_LABEL (summary)), ==, "No physics body");

	/* its button (below the fold of the inspector's scroll box) adds a
	 * body through the panel's action */
	g_signal_emit_by_name (efx_find (f, "inspector-physics-open"), "clicked");
	g_assert_nonnull (lrg_node_get_physics (cube));
	efx_frames (f, 2);
	g_assert_nonnull (strstr (lrg_label_get_text (LRG_LABEL (summary)), "dynamic body"));
	g_assert_nonnull (strstr (lrg_label_get_text (LRG_LABEL (summary)), "1 shape: box"));
	g_assert_cmpint (lrg_editor_physics_panel_add_joint (panel, NULL, LRG_JOINT_HINGE, NULL), ==, 0);
	efx_frames (f, 2);
	g_assert_nonnull (strstr (lrg_label_get_text (LRG_LABEL (summary)), "1 joint: hinge"));

	/* the hierarchy badge: hierarchy.state reports the body kind */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'hierarchy.state'}");

		rows = json_object_get_array_member (json_node_get_object (r), "rows");
		for (i = 0; i < json_array_get_length (rows); i++)
		{
			JsonObject *row = json_array_get_object_element (rows, i);

			if (g_strcmp0 (json_object_get_string_member (row, "name"), "Crate") == 0)
			{
				g_assert_cmpstr (json_object_get_string_member (row, "physics"), ==, "dynamic");
				found++;
			}
			else if (g_strcmp0 (json_object_get_string_member (row, "name"), "Ground") == 0)
			{
				g_assert_true (json_node_is_null (json_object_get_member (row, "physics")));
				found++;
			}
		}
	}
	g_assert_cmpuint (found, ==, 2);
	lrg_canvas_record (lrg_editor_app_get_canvas (f->app));
}

/* ==========================================================================
 * Play and round trip
 * ========================================================================== */

static void
test_play (EditorAppFixture *f,
           gconstpointer     data)
{
	g_autoptr(LrgWidget)   old = NULL;
	LrgEditorViewportSplit *split = fx_split (f, &old);
	LrgEditorViewportPanel *vp = lrg_editor_viewport_split_get_panel (split, 0);
	LrgEditorPhysicsPanel  *panel = phys_panel (f);
	LrgNode                *cube, *ground;
	gchar                  *text;

	cube = crate_level (f, &ground);
	g_assert_true (lrg_editor_physics_panel_add_body (panel, cube, LRG_DYNAMICS_BODY_DYNAMIC, TRUE));
	g_assert_true (lrg_editor_physics_panel_add_body (panel, ground, LRG_DYNAMICS_BODY_STATIC, TRUE));
	lrg_editor_select (f->editor, cube, FALSE);
	efx_frames (f, 2);
	g_assert_null (lrg_editor_physics_panel_describe_live (panel));

	if (!lrg_editor_app_activate_action (f->app, "play.start") || !lrg_editor_app_is_playing (f->app))
	{
		g_test_skip ("play-in-editor is not available here");
		return;
	}
	efx_frames (f, 20);

	/* the live body falls */
	{
		g_autoptr(JsonNode) r = efx_ipc_result (f, "{'id':1,'method':'physics.state','params':{'node':'Crate'}}");
		JsonObject         *o = json_node_get_object (r);
		JsonObject         *b;

		g_assert_true (json_object_get_boolean_member (o, "playing"));
		if (json_node_is_null (json_object_get_member (o, "body")))
		{
			g_test_skip ("the play world has no 3D physics here");
			lrg_editor_app_activate_action (f->app, "play.stop");
			return;
		}
		b = json_object_get_object_member (o, "body");
		g_assert_cmpfloat (json_array_get_double_element (json_object_get_array_member (b, "position"), 1), <, 2.0);
	}
	text = lrg_editor_physics_panel_describe_live (panel);
	g_assert_nonnull (text);
	g_assert_nonnull (strstr (text, "Velocity"));
	g_free (text);
	g_assert_true (lrg_widget_get_visible (efx_find (f, "physics-live-section")));

	/* the overlay records the play world */
	lrg_editor_viewport_panel_set_physics_overlay (vp, LRG_DYNAMICS_DEBUG_SHAPES | LRG_DYNAMICS_DEBUG_CONTACTS);
	g_assert_cmpuint (lrg_editor_viewport_panel_record_physics (vp), >=, 2);
	g_assert_cmpuint (lrg_debug_draw_get_count (lrg_editor_viewport_panel_get_physics_debug_draw (vp)), >=, 2);

	g_assert_true (lrg_editor_app_activate_action (f->app, "play.stop"));
	g_assert_cmpuint (lrg_editor_viewport_panel_record_physics (vp), ==, 0);
	g_assert_false (lrg_widget_get_visible (efx_find (f, "physics-live-section")));
}

static void
test_round_trip (EditorAppFixture *f,
                 gconstpointer     data)
{
	LrgEditorPhysicsPanel *panel = phys_panel (f);
	g_autoptr(GError)      error = NULL;
	g_autofree gchar      *path = g_build_filename (f->dir, "phys.rlevel", NULL);
	LrgNode               *ground, *loaded;
	g_auto(GValue)         m = G_VALUE_INIT;

	g_assert_nonnull (crate_level (f, &ground));
	g_assert_true (lrg_editor_physics_panel_add_body (panel, NULL, LRG_DYNAMICS_BODY_DYNAMIC, TRUE));
	gv_double (&m, 42.0);
	g_assert_true (lrg_editor_physics_panel_set_body_field (panel, NULL, "mass", &m));
	g_assert_cmpint (lrg_editor_physics_panel_add_joint (panel, NULL, LRG_JOINT_HINGE, "Ground"), ==, 0);
	g_assert_true (lrg_editor_save_level (f->editor, path, &error));
	g_assert_no_error (error);

	g_assert_true (lrg_editor_load_level (f->editor, path, &error));
	g_assert_no_error (error);
	loaded = lrg_editor_find_node (f->editor, "Crate");
	g_assert_nonnull (loaded);
	g_assert_nonnull (lrg_node_get_physics (loaded));
	g_assert_cmpfloat_with_epsilon (lrg_node_physics_get_mass (lrg_node_get_physics (loaded)), 42.0f, 1e-4f);
	g_assert_cmpuint (lrg_node_physics_get_shapes (lrg_node_get_physics (loaded))->len, ==, 1);
	g_assert_cmpstr (lrg_node_physics_joint_get_target (joint_of (loaded, 0)), ==,
	                 lrg_node_get_guid (lrg_editor_find_node (f->editor, "Ground")));

	/* the panel follows the replaced level */
	lrg_editor_select (f->editor, loaded, FALSE);
	efx_frames (f, 2);
	g_assert_true (lrg_editor_physics_panel_get_node (panel) == loaded);
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
	InitWindow (64, 64, "test-editor-app-physics");
	return IsWindowReady ();
}

static void
test_gpu_overlay (EditorAppFixture *f,
                  gconstpointer     data)
{
	LrgEditorPhysicsPanel *panel;
	LrgNode               *cube, *ground;
	g_autofree gchar      *shot = NULL;

	if (!gpu_ready)
	{
		g_test_skip ("needs the isolated X display (xrun.sh)");
		return;
	}
	panel = phys_panel (f);
	cube = crate_level (f, &ground);
	g_assert_true (lrg_editor_physics_panel_add_body (panel, cube, LRG_DYNAMICS_BODY_DYNAMIC, TRUE));
	g_assert_true (lrg_editor_physics_panel_add_body (panel, ground, LRG_DYNAMICS_BODY_STATIC, TRUE));
	g_assert_cmpint (lrg_editor_physics_panel_add_joint (panel, cube, LRG_JOINT_HINGE, "Ground"), ==, 0);
	frame_front (f);
	efx_ipc_result (f, "{'id':1,'method':'physics.overlay','params':{'flags':['all']}}");
	efx_frames (f, 3);

	/* fit a mesh-asset-free node with a window: the bounds come from the
	 * primitive (no model file) */
	g_assert_true (lrg_editor_physics_panel_fit_shape (panel, cube, 0, LRG_COLLIDER3D_BOX));

	/* while playing, the frames render the play world with the recorded
	 * physics drawn from the runtime renderer's overlay hook; a viewport
	 * screenshot reads that image back where the viewport has one */
	shot = g_build_filename (f->dir, "phys.png", NULL);
	if (lrg_editor_app_activate_action (f->app, "play.start"))
	{
		g_autofree gchar *req = g_strdup_printf ("{'id':1,'method':'view.screenshot','params':{'path':'%s'}}", shot);
		g_autofree gchar *reply = NULL;

		efx_frames (f, 10);
		reply = efx_ipc (f, req);
		if (strstr (reply, "\"error\"") == NULL)
			g_assert_true (g_file_test (shot, G_FILE_TEST_EXISTS));
		lrg_editor_app_activate_action (f->app, "play.stop");
	}
	efx_frames (f, 2);
}

int
main (int   argc,
      char *argv[])
{
	gint result;

	g_test_init (&argc, &argv, NULL);
	g_setenv ("LRG_EXTERNAL_EDITOR", "none", TRUE);   /* no editor window from a test */
	gpu_ready = gpu_init ();
	g_test_add ("/editor-app/physics/body", EditorAppFixture, NULL, efx_setup, test_body, efx_teardown);
	g_test_add ("/editor-app/physics/fields", EditorAppFixture, NULL, efx_setup, test_fields, efx_teardown);
	g_test_add ("/editor-app/physics/shapes", EditorAppFixture, NULL, efx_setup, test_shapes, efx_teardown);
	g_test_add ("/editor-app/physics/joints", EditorAppFixture, NULL, efx_setup, test_joints, efx_teardown);
	g_test_add ("/editor-app/physics/ipc", EditorAppFixture, NULL, efx_setup, test_ipc, efx_teardown);
	g_test_add ("/editor-app/physics/gizmo", EditorAppFixture, NULL, efx_setup, test_gizmo, efx_teardown);
	g_test_add ("/editor-app/physics/overlay", EditorAppFixture, NULL, efx_setup, test_overlay, efx_teardown);
	g_test_add ("/editor-app/physics/inspector-hierarchy", EditorAppFixture, NULL, efx_setup,
	            test_inspector_hierarchy, efx_teardown);
	g_test_add ("/editor-app/physics/play", EditorAppFixture, NULL, efx_setup, test_play, efx_teardown);
	g_test_add ("/editor-app/physics/round-trip", EditorAppFixture, NULL, efx_setup, test_round_trip, efx_teardown);
	g_test_add ("/editor-app/physics/gpu-overlay", EditorAppFixture, NULL, efx_setup, test_gpu_overlay, efx_teardown);

	result = g_test_run ();
	if (gpu_ready)
		CloseWindow ();
	return result;
}
