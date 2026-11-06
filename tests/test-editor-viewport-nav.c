/* test-editor-viewport-nav.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Pure-logic tests for LrgEditorViewportNav. Every navigation scenario of
 * tests/test-editor-app.c (viewport-rotate, nav-gizmo,
 * nav-keys-and-auto-perspective, nav-schemes) and tests/editor-x11-smoke.sh
 * (right drag, gizmo drag, KP_7, wheel) is reproduced here as events, plus
 * held-key flying over frames, focus loss, invert-y, box selection and 2D.
 */

#include <glib.h>
#include <string.h>
#include <math.h>
#include <libregnum.h>

#define W     (800.0f)
#define H     (600.0f)
#define CX    (W * 0.5f)
#define CY    (H * 0.5f)
#define DT    (1.0f / 60.0f)
#define EPS   (1e-3f)
#define NOMOD (LRG_INPUT_MODIFIER_NONE)

typedef struct
{
	LrgEditorViewState   *vs;
	LrgEditorViewportNav *nav;
	guint                 clicks;
	guint                 boxes;
	guint                 menus;
	guint                 camera_changes;
	guint                 mode_changes;
	gfloat                box[4];
} Fixture;

static void
on_click (LrgEditorViewportNav *nav,
          gfloat                x,
          gfloat                y,
          LrgInputModifiers     mods,
          Fixture              *f)
{
	f->clicks++;
}

static void
on_box (LrgEditorViewportNav *nav,
        gfloat                x0,
        gfloat                y0,
        gfloat                x1,
        gfloat                y1,
        LrgInputModifiers     mods,
        Fixture              *f)
{
	f->boxes++;
	f->box[0] = x0;
	f->box[1] = y0;
	f->box[2] = x1;
	f->box[3] = y1;
}

static void
on_menu (LrgEditorViewportNav *nav,
         gfloat                x,
         gfloat                y,
         Fixture              *f)
{
	f->menus++;
}

static void
on_camera (LrgEditorViewportNav *nav,
           Fixture              *f)
{
	f->camera_changes++;
}

static void
on_mode (LrgEditorViewportNav *nav,
         LrgEditorNavMode      mode,
         Fixture              *f)
{
	f->mode_changes++;
}

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
	memset (f, 0, sizeof *f);
	f->vs = lrg_editor_view_state_new (NULL);
	lrg_editor_view_state_set_viewport_size (f->vs, -1, W, H);
	f->nav = lrg_editor_viewport_nav_new (f->vs);
	g_signal_connect (f->nav, "click", G_CALLBACK (on_click), f);
	g_signal_connect (f->nav, "box-select", G_CALLBACK (on_box), f);
	g_signal_connect (f->nav, "context-menu", G_CALLBACK (on_menu), f);
	g_signal_connect (f->nav, "camera-changed", G_CALLBACK (on_camera), f);
	g_signal_connect (f->nav, "mode-changed", G_CALLBACK (on_mode), f);
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
	g_clear_object (&f->nav);
	g_clear_object (&f->vs);
}

static LrgEditorViewCamera *
cam (Fixture *f)
{
	return lrg_editor_viewport_nav_get_camera (f->nav);
}

/* A snapshot of the active camera (the caller frees it). */
static LrgEditorViewCamera *
snap (Fixture *f)
{
	return lrg_editor_view_camera_copy (cam (f));
}

/* Presses @button at (x, y), drags @steps times by (sx, sy), releases. */
static void
drag (Fixture           *f,
      GrlMouseButton     button,
      LrgInputModifiers  mods,
      gfloat             x,
      gfloat             y,
      gint               steps,
      gfloat             sx,
      gfloat             sy)
{
	gint i;

	lrg_editor_viewport_nav_pointer_press (f->nav, button, x, y, mods);
	for (i = 1; i <= steps; i++)
		g_assert_true (lrg_editor_viewport_nav_pointer_move (f->nav, x + i * sx, y + i * sy, sx, sy, mods));
	lrg_editor_viewport_nav_pointer_release (f->nav, button, x + steps * sx, y + steps * sy, mods);
}

/* ==========================================================================
 * Default scheme
 * ========================================================================== */

/* test-editor-app "viewport-rotate" and smoke step 5. */
static void
test_nav_right_drag_flies (Fixture       *f,
                           gconstpointer  data)
{
	g_autoptr(LrgEditorViewCamera) before = snap (f);
	g_autoptr(LrgEditorViewCamera) still = NULL;
	gint                           i;

	g_assert_true (lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX, CY, NOMOD));
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_FLY);
	g_assert_cmpint (lrg_editor_view_state_get_nav_mode (f->vs), ==, LRG_EDITOR_NAV_MODE_FLY);
	for (i = 1; i <= 10; i++)
		lrg_editor_viewport_nav_pointer_move (f->nav, CX + i * 10.0f, CY + i * 3.0f, 10.0f, 3.0f, NOMOD);
	g_assert_true (lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX + 100.0f,
	                                                        CY + 30.0f, NOMOD));
	g_assert_false (lrg_editor_view_camera_equal (before, cam (f)));
	/* Looking turns the target around the camera position. */
	g_assert_cmpmem (before->view.position, sizeof before->view.position, cam (f)->view.position,
	                 sizeof before->view.position);
	g_assert_cmpuint (f->menus, ==, 0);
	g_assert_cmpint (lrg_editor_view_state_get_nav_mode (f->vs), ==, LRG_EDITOR_NAV_MODE_NONE);

	/* A right click without motion opens the menu and moves nothing. */
	still = snap (f);
	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX, CY, NOMOD);
	lrg_editor_viewport_nav_pointer_move (f->nav, CX + 1.0f, CY, 0.0f, 0.0f, NOMOD);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX + 1.0f, CY, NOMOD);
	g_assert_true (lrg_editor_view_camera_equal (still, cam (f)));
	g_assert_cmpuint (f->menus, ==, 1);
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_NONE);
}

/* Held WASD/QE over frames, Shift boost, wheel speed, key consumption. */
static void
test_nav_fly_keys (Fixture       *f,
                   gconstpointer  data)
{
	g_autoptr(LrgEditorViewCamera) start = NULL;
	gfloat                         fwd[3], moved;
	gint                           i;

	/* Not flying: keys are recorded but not consumed, tick does nothing. */
	g_assert_false (lrg_editor_viewport_nav_key_down (f->nav, GRL_KEY_W, NOMOD));
	g_assert_false (lrg_editor_viewport_nav_tick (f->nav, DT));
	lrg_editor_viewport_nav_key_up (f->nav, GRL_KEY_W, NOMOD);

	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX, CY, NOMOD);
	start = snap (f);
	for (i = 0; i < 3; i++)
		fwd[i] = start->view.target[i] - start->view.position[i];
	moved = sqrtf (fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
	for (i = 0; i < 3; i++)
		fwd[i] /= moved;

	/* W held for 30 frames: 30/60 s at 8 units/s = 4 units forward. */
	g_assert_true (lrg_editor_viewport_nav_key_down (f->nav, GRL_KEY_W, NOMOD));
	g_assert_true (lrg_editor_viewport_nav_is_key_held (f->nav, GRL_KEY_W));
	for (i = 0; i < 30; i++)
		g_assert_true (lrg_editor_viewport_nav_tick (f->nav, DT));
	moved = (cam (f)->view.position[0] - start->view.position[0]) * fwd[0] +
	        (cam (f)->view.position[1] - start->view.position[1]) * fwd[1] +
	        (cam (f)->view.position[2] - start->view.position[2]) * fwd[2];
	g_assert_cmpfloat_with_epsilon (moved, 4.0f, EPS);

	/* Shift: three times faster. */
	lrg_editor_view_camera_free (start);
	start = snap (f);
	lrg_editor_viewport_nav_key_down (f->nav, GRL_KEY_LEFT_SHIFT, LRG_INPUT_MODIFIER_SHIFT);
	for (i = 0; i < 30; i++)
		lrg_editor_viewport_nav_tick (f->nav, DT);
	moved = (cam (f)->view.position[0] - start->view.position[0]) * fwd[0] +
	        (cam (f)->view.position[1] - start->view.position[1]) * fwd[1] +
	        (cam (f)->view.position[2] - start->view.position[2]) * fwd[2];
	g_assert_cmpfloat_with_epsilon (moved, 12.0f, 1e-2f);
	lrg_editor_viewport_nav_key_up (f->nav, GRL_KEY_LEFT_SHIFT, NOMOD);

	/* E rises along world Y; releasing keys stops the motion. */
	g_assert_true (lrg_editor_viewport_nav_key_up (f->nav, GRL_KEY_W, NOMOD));
	lrg_editor_view_camera_free (start);
	start = snap (f);
	lrg_editor_viewport_nav_key_down (f->nav, GRL_KEY_E, NOMOD);
	lrg_editor_viewport_nav_tick (f->nav, 0.5f);
	g_assert_cmpfloat_with_epsilon (cam (f)->view.position[1] - start->view.position[1], 4.0f, EPS);
	lrg_editor_viewport_nav_key_up (f->nav, GRL_KEY_E, NOMOD);
	g_assert_false (lrg_editor_viewport_nav_tick (f->nav, DT));

	/* Shortcut keys that are not fly keys are not consumed. */
	g_assert_false (lrg_editor_viewport_nav_key_down (f->nav, GRL_KEY_F, NOMOD));

	/* The wheel tunes the fly speed while flying, not the camera. */
	lrg_editor_view_camera_free (start);
	start = snap (f);
	g_assert_true (lrg_editor_viewport_nav_scroll (f->nav, 0.0f, 1.0f, NOMOD));
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_state_get_fly_speed (f->vs), 8.0 * 1.2, 1e-4);
	g_assert_true (lrg_editor_view_camera_equal (start, cam (f)));

	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX, CY, NOMOD);
	g_assert_false (lrg_editor_viewport_nav_key_down (f->nav, GRL_KEY_W, NOMOD));
}

static void
test_nav_focus_lost (Fixture       *f,
                     gconstpointer  data)
{
	g_autoptr(LrgEditorViewCamera) start = NULL;

	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX, CY, NOMOD);
	lrg_editor_viewport_nav_key_down (f->nav, GRL_KEY_W, NOMOD);
	lrg_editor_viewport_nav_key_down (f->nav, GRL_KEY_D, NOMOD);
	g_assert_true (lrg_editor_viewport_nav_tick (f->nav, DT));

	/* Focus goes away mid-flight: keys and the gesture are forgotten. */
	lrg_editor_viewport_nav_focus_lost (f->nav);
	g_assert_false (lrg_editor_viewport_nav_is_key_held (f->nav, GRL_KEY_W));
	g_assert_false (lrg_editor_viewport_nav_is_key_held (f->nav, GRL_KEY_D));
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_NONE);
	g_assert_cmpint (lrg_editor_view_state_get_nav_mode (f->vs), ==, LRG_EDITOR_NAV_MODE_NONE);
	g_assert_cmpuint (f->menus, ==, 0);

	/* Flying again does not move without a fresh key press. */
	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX, CY, NOMOD);
	start = snap (f);
	g_assert_false (lrg_editor_viewport_nav_tick (f->nav, DT));
	g_assert_true (lrg_editor_view_camera_equal (start, cam (f)));

	/* The release that arrives after a focus loss is not navigation's. */
	lrg_editor_viewport_nav_focus_lost (f->nav);
	g_assert_false (lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX, CY, NOMOD));
	g_assert_cmpuint (f->menus, ==, 0);
}

static void
test_nav_orbit_pan_dolly (Fixture       *f,
                          gconstpointer  data)
{
	g_autoptr(LrgEditorViewCamera) start = snap (f);
	g_autoptr(LrgEditorViewCamera) step = NULL;
	gfloat                         dist = lrg_editor_view_camera_get_distance (start);

	/* Alt + left orbits around the target, only past the click slop. */
	g_assert_true (lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_LEFT, CX, CY,
	                                                      LRG_INPUT_MODIFIER_ALT));
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_ORBIT);
	lrg_editor_viewport_nav_pointer_move (f->nav, CX + 2.0f, CY, 2.0f, 0.0f, LRG_INPUT_MODIFIER_ALT);
	g_assert_true (lrg_editor_view_camera_equal (start, cam (f)));
	lrg_editor_viewport_nav_pointer_move (f->nav, CX + 20.0f, CY, 18.0f, 0.0f, LRG_INPUT_MODIFIER_ALT);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_LEFT, CX + 20.0f, CY, NOMOD);
	g_assert_false (lrg_editor_view_camera_equal (start, cam (f)));
	g_assert_cmpmem (start->view.target, sizeof start->view.target, cam (f)->view.target,
	                 sizeof start->view.target);
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_distance (cam (f)), dist, EPS);
	/* Dragging right orbits by -dx * 0.35 degrees per pixel. */
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_yaw (cam (f)),
	                                lrg_editor_view_camera_get_yaw (start) - 18.0f * 0.35f * (gfloat) G_PI / 180.0f,
	                                EPS);
	g_assert_cmpuint (f->clicks + f->boxes, ==, 0);

	/* Alt + Shift + left pans: camera and target move together. */
	step = snap (f);
	drag (f, GRL_MOUSE_BUTTON_LEFT, LRG_INPUT_MODIFIER_ALT | LRG_INPUT_MODIFIER_SHIFT, CX, CY, 5, 6.0f, 0.0f);
	g_assert_false (lrg_editor_view_camera_equal (step, cam (f)));
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_distance (cam (f)), dist, EPS);
	g_assert_cmpfloat (fabsf (cam (f)->view.target[0] - step->view.target[0]) +
	                   fabsf (cam (f)->view.target[2] - step->view.target[2]), >, 0.0f);

	/* Middle drag pans in the default scheme. */
	lrg_editor_view_camera_free (step);
	step = snap (f);
	g_assert_true (lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_MIDDLE, CX, CY, NOMOD));
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_PAN);
	lrg_editor_viewport_nav_pointer_move (f->nav, CX, CY + 10.0f, 0.0f, 10.0f, NOMOD);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_MIDDLE, CX, CY + 10.0f, NOMOD);
	g_assert_false (lrg_editor_view_camera_equal (step, cam (f)));

	/* The wheel dollies by 0.87 per notch (smoke step 8). */
	dist = lrg_editor_view_camera_get_distance (cam (f));
	g_assert_true (lrg_editor_viewport_nav_scroll (f->nav, 0.0f, 2.0f, NOMOD));
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_distance (cam (f)), dist * 0.87f * 0.87f, EPS);
	/* A horizontal scroll means nothing in the default scheme. */
	g_assert_false (lrg_editor_viewport_nav_scroll (f->nav, 3.0f, 0.0f, NOMOD));
	g_assert_cmpuint (f->camera_changes, >=, 4);
}

static void
test_nav_invert_y (Fixture       *f,
                   gconstpointer  data)
{
	g_autoptr(LrgEditorViewCamera) start = snap (f);
	gfloat                         normal, inverted;

	drag (f, GRL_MOUSE_BUTTON_LEFT, LRG_INPUT_MODIFIER_ALT, CX, CY, 4, 0.0f, 5.0f);
	normal = lrg_editor_view_camera_get_pitch (cam (f)) - lrg_editor_view_camera_get_pitch (start);
	lrg_editor_view_state_set_camera (f->vs, -1, start);

	lrg_editor_viewport_nav_set_invert_y (f->nav, TRUE);
	drag (f, GRL_MOUSE_BUTTON_LEFT, LRG_INPUT_MODIFIER_ALT, CX, CY, 4, 0.0f, 5.0f);
	inverted = lrg_editor_view_camera_get_pitch (cam (f)) - lrg_editor_view_camera_get_pitch (start);
	g_assert_cmpfloat (normal, >, 0.0f);
	g_assert_cmpfloat_with_epsilon (inverted, -normal, EPS);
}

/* ==========================================================================
 * Schemes
 * ========================================================================== */

/* test-editor-app "nav-schemes", extended to every scheme gesture. */
static void
test_nav_schemes (Fixture       *f,
                  gconstpointer  data)
{
	g_autoptr(LrgEditorSettings)   settings = lrg_editor_settings_new ();
	g_autoptr(LrgEditorViewCamera) a = NULL;
	gfloat                         dist;

	lrg_editor_viewport_nav_bind_settings (f->nav, settings);
	g_object_set (settings, "camera-navigation", "trackpad", "camera-orbit-speed", 0.5,
	              "camera-invert-y", TRUE, NULL);
	g_assert_cmpint (lrg_editor_viewport_nav_get_scheme (f->nav), ==, LRG_EDITOR_NAV_SCHEME_TRACKPAD);
	g_assert_cmpfloat (lrg_editor_viewport_nav_get_orbit_speed (f->nav), ==, 0.5);
	g_assert_true (lrg_editor_viewport_nav_get_invert_y (f->nav));
	g_object_set (settings, "camera-invert-y", FALSE, NULL);

	/* Trackpad: two-finger scroll orbits (target fixed, distance kept). */
	a = snap (f);
	dist = lrg_editor_view_camera_get_distance (a);
	g_assert_true (lrg_editor_viewport_nav_scroll (f->nav, 2.0f, 1.0f, NOMOD));
	g_assert_false (lrg_editor_view_camera_equal (a, cam (f)));
	g_assert_cmpmem (a->view.target, sizeof a->view.target, cam (f)->view.target, sizeof a->view.target);
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_distance (cam (f)), dist, EPS);
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_yaw (cam (f)) - lrg_editor_view_camera_get_yaw (a),
	                                8.0f * (gfloat) G_PI / 180.0f, EPS);

	/* Shift + scroll pans; Ctrl + scroll zooms. */
	lrg_editor_view_camera_free (a);
	a = snap (f);
	lrg_editor_viewport_nav_scroll (f->nav, 1.0f, 0.0f, LRG_INPUT_MODIFIER_SHIFT);
	g_assert_false (lrg_editor_view_camera_equal (a, cam (f)));
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_distance (cam (f)), dist, EPS);
	lrg_editor_viewport_nav_scroll (f->nav, 0.0f, 1.0f, LRG_INPUT_MODIFIER_CTRL);
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_distance (cam (f)), dist * 0.87f, EPS);

	/* Blender: middle drag orbits... */
	g_object_set (settings, "camera-navigation", "blender", NULL);
	lrg_editor_view_camera_free (a);
	a = snap (f);
	dist = lrg_editor_view_camera_get_distance (a);
	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_MIDDLE, CX, CY, NOMOD);
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_ORBIT);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_MIDDLE, CX, CY, NOMOD);
	drag (f, GRL_MOUSE_BUTTON_MIDDLE, NOMOD, CX, CY, 8, 8.0f, 0.0f);
	g_assert_false (lrg_editor_view_camera_equal (a, cam (f)));
	g_assert_cmpmem (a->view.target, sizeof a->view.target, cam (f)->view.target, sizeof a->view.target);

	/* ...Shift + middle pans... */
	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_MIDDLE, CX, CY, LRG_INPUT_MODIFIER_SHIFT);
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_PAN);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_MIDDLE, CX, CY, NOMOD);

	/* ...and Ctrl + middle zooms by 1.01 per pixel of vertical drag. */
	dist = lrg_editor_view_camera_get_distance (cam (f));
	drag (f, GRL_MOUSE_BUTTON_MIDDLE, LRG_INPUT_MODIFIER_CTRL, CX, CY, 2, 0.0f, 10.0f);
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_distance (cam (f)),
	                                dist * powf (1.01f, 10.0f) * powf (1.01f, 10.0f), EPS);

	/* The wheel still dollies in the blender scheme. */
	dist = lrg_editor_view_camera_get_distance (cam (f));
	lrg_editor_viewport_nav_scroll (f->nav, 0.0f, 1.0f, NOMOD);
	g_assert_cmpfloat_with_epsilon (lrg_editor_view_camera_get_distance (cam (f)), dist * 0.87f, EPS);

	/* Unknown setting values fall back to the default scheme; unbinding
	 * stops following the settings. */
	g_object_set (settings, "camera-navigation", "maya", NULL);
	g_assert_cmpint (lrg_editor_viewport_nav_get_scheme (f->nav), ==, LRG_EDITOR_NAV_SCHEME_DEFAULT);
	lrg_editor_viewport_nav_bind_settings (f->nav, NULL);
	g_object_set (settings, "camera-navigation", "blender", NULL);
	g_assert_cmpint (lrg_editor_viewport_nav_get_scheme (f->nav), ==, LRG_EDITOR_NAV_SCHEME_DEFAULT);
}

/* ==========================================================================
 * Orientation gizmo and auto-perspective
 * ========================================================================== */

/* test-editor-app "nav-gizmo" and smoke step 6. */
static void
test_nav_gizmo (Fixture       *f,
                gconstpointer  data)
{
	g_autoptr(LrgEditorViewCamera) before = snap (f);
	gfloat                         gx, gy, ex, ey;
	gint                           i;

	g_assert_true (lrg_editor_viewport_nav_get_gizmo_center (f->nav, &gx, &gy));
	g_assert_cmpfloat (gx, ==, 46.0f);
	g_assert_cmpfloat (gy, ==, H - 46.0f);

	/* Hovering the disc lights it; far away nothing is hot. */
	lrg_editor_viewport_nav_pointer_move (f->nav, gx + 3.0f, gy + 3.0f, 0.0f, 0.0f, NOMOD);
	g_assert_cmpint (lrg_editor_viewport_nav_get_axis_hot (f->nav), !=, -1);
	lrg_editor_viewport_nav_pointer_move (f->nav, CX, CY, 0.0f, 0.0f, NOMOD);
	g_assert_cmpint (lrg_editor_viewport_nav_get_axis_hot (f->nav), ==, -1);

	/* Dragging the gizmo orbits and never selects. */
	g_assert_true (lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_LEFT, gx + 3.0f, gy + 3.0f,
	                                                      NOMOD));
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_GIZMO);
	for (i = 1; i <= 8; i++)
		lrg_editor_viewport_nav_pointer_move (f->nav, gx + 3.0f + i * 6.0f, gy + 3.0f, 6.0f, 0.0f, NOMOD);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_LEFT, gx + 51.0f, gy + 3.0f, NOMOD);
	g_assert_false (lrg_editor_view_camera_equal (before, cam (f)));
	g_assert_cmpuint (f->clicks + f->boxes, ==, 0);

	/* Clicking the +Y end looks from the top, orthographic. */
	lrg_editor_viewport_nav_get_gizmo_end (f->nav, 1, &ex, &ey, NULL);
	g_assert_cmpint (lrg_editor_viewport_nav_gizmo_hit (f->nav, gx + ex, gy + ey), ==, 1);
	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_LEFT, gx + ex, gy + ey, NOMOD);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_LEFT, gx + ex, gy + ey, NOMOD);
	g_assert_cmpint (cam (f)->axis_view, ==, LRG_EDITOR_AXIS_VIEW_TOP);
	g_assert_true (cam (f)->view.orthographic);

	/* Auto-perspective: orbiting out of the axis view leaves ortho. */
	drag (f, GRL_MOUSE_BUTTON_LEFT, LRG_INPUT_MODIFIER_ALT, CX, CY, 3, 5.0f, 0.0f);
	g_assert_false (cam (f)->view.orthographic);

	/* An explicit KP_5 survives orbiting (test-editor-app keys case). */
	lrg_editor_view_state_toggle_ortho (f->vs);
	drag (f, GRL_MOUSE_BUTTON_LEFT, LRG_INPUT_MODIFIER_ALT, CX, CY, 3, 5.0f, 0.0f);
	g_assert_true (cam (f)->view.orthographic);

	/* KP_7 then KP_4 (smoke step 7, test-editor-app keys case). */
	lrg_editor_view_state_set_axis_view (f->vs, LRG_EDITOR_AXIS_VIEW_TOP);
	g_assert_true (cam (f)->view.orthographic);
	lrg_editor_view_state_orbit_step (f->vs, -1, 0);
	g_assert_false (cam (f)->view.orthographic);
}

/* ==========================================================================
 * Box selection, click, 2D
 * ========================================================================== */

static void
test_nav_box_and_click (Fixture       *f,
                        gconstpointer  data)
{
	gfloat x, y, w, h;

	/* A plain left press is not navigation's... */
	g_assert_false (lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_LEFT, 300.0f, 200.0f, NOMOD));
	/* ...the caller's gizmo missed, so box selection is armed. */
	lrg_editor_viewport_nav_press_empty (f->nav, 300.0f, 200.0f);
	g_assert_true (lrg_editor_viewport_nav_pointer_move (f->nav, 303.0f, 202.0f, 3.0f, 2.0f, NOMOD));
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_NONE);
	g_assert_false (lrg_editor_viewport_nav_get_box_rect (f->nav, NULL, NULL, NULL, NULL));
	lrg_editor_viewport_nav_pointer_move (f->nav, 250.0f, 260.0f, -53.0f, 58.0f, NOMOD);
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_BOX);
	g_assert_cmpint (lrg_editor_view_state_get_nav_mode (f->vs), ==, LRG_EDITOR_NAV_MODE_BOX);
	g_assert_true (lrg_editor_viewport_nav_get_box_rect (f->nav, &x, &y, &w, &h));
	g_assert_cmpfloat (x, ==, 250.0f);
	g_assert_cmpfloat (y, ==, 200.0f);
	g_assert_cmpfloat (w, ==, 50.0f);
	g_assert_cmpfloat (h, ==, 60.0f);

	/* Other buttons during the box are swallowed. */
	g_assert_true (lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_RIGHT, 250.0f, 260.0f, NOMOD));
	g_assert_true (lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_RIGHT, 250.0f, 260.0f,
	                                                        NOMOD));
	g_assert_true (lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_LEFT, 250.0f, 260.0f,
	                                                        LRG_INPUT_MODIFIER_SHIFT));
	g_assert_cmpuint (f->boxes, ==, 1);
	g_assert_cmpuint (f->clicks, ==, 0);
	g_assert_cmpfloat (f->box[0], ==, 300.0f);
	g_assert_cmpfloat (f->box[3], ==, 260.0f);
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_NONE);

	/* Within the slop it is a click. */
	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_LEFT, 100.0f, 100.0f, NOMOD);
	lrg_editor_viewport_nav_press_empty (f->nav, 100.0f, 100.0f);
	lrg_editor_viewport_nav_pointer_move (f->nav, 104.0f, 100.0f, 4.0f, 0.0f, NOMOD);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_LEFT, 104.0f, 100.0f, NOMOD);
	g_assert_cmpuint (f->clicks, ==, 1);
	g_assert_cmpuint (f->boxes, ==, 1);

	/* A release with nothing going on is not consumed. */
	g_assert_false (lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_LEFT, 0.0f, 0.0f, NOMOD));
}

static void
test_nav_2d (Fixture       *f,
             gconstpointer  data)
{
	g_autoptr(LrgEditorViewCamera) before = NULL;
	gfloat                         gx, gy;

	lrg_editor_view_state_toggle_2d (f->vs);
	before = snap (f);

	/* No orientation gizmo in 2D. */
	g_assert_false (lrg_editor_viewport_nav_get_gizmo_center (f->nav, &gx, &gy));
	g_assert_cmpint (lrg_editor_viewport_nav_gizmo_hit (f->nav, gx, gy), ==, -1);

	/* Right and Alt + left drags pan; blender middle pans too. */
	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX, CY, NOMOD);
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_PAN);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX, CY, NOMOD);
	g_assert_cmpuint (f->menus, ==, 1);
	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_LEFT, CX, CY, LRG_INPUT_MODIFIER_ALT);
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_PAN);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_LEFT, CX, CY, NOMOD);
	lrg_editor_viewport_nav_set_scheme (f->nav, LRG_EDITOR_NAV_SCHEME_BLENDER);
	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_MIDDLE, CX, CY, NOMOD);
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_PAN);
	lrg_editor_viewport_nav_pointer_move (f->nav, CX + 10.0f, CY, 10.0f, 0.0f, NOMOD);
	lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_MIDDLE, CX + 10.0f, CY, NOMOD);
	g_assert_false (lrg_editor_view_camera_equal (before, cam (f)));
	g_assert_true (cam (f)->view_2d);
	g_assert_true (cam (f)->view.orthographic);

	/* The trackpad scheme pans on scroll in 2D; the wheel zooms the
	 * orthographic height. */
	lrg_editor_viewport_nav_set_scheme (f->nav, LRG_EDITOR_NAV_SCHEME_TRACKPAD);
	lrg_editor_view_camera_free (before);
	before = snap (f);
	lrg_editor_viewport_nav_scroll (f->nav, 1.0f, 0.0f, NOMOD);
	g_assert_cmpfloat (cam (f)->view.position[0], !=, before->view.position[0]);
	lrg_editor_viewport_nav_set_scheme (f->nav, LRG_EDITOR_NAV_SCHEME_DEFAULT);
	lrg_editor_viewport_nav_scroll (f->nav, 0.0f, 1.0f, NOMOD);
	g_assert_cmpfloat_with_epsilon (cam (f)->view.ortho_height, 20.0f * 0.87f, EPS);
}

/* Navigation follows the active viewport of a split layout. */
static void
test_nav_active_viewport (Fixture       *f,
                          gconstpointer  data)
{
	g_autoptr(LrgEditorViewCamera) first = NULL;

	lrg_editor_view_state_set_layout (f->vs, LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_H);
	lrg_editor_view_state_set_active_viewport (f->vs, 1);
	lrg_editor_view_state_set_viewport_size (f->vs, 1, W * 0.5f, H);
	first = lrg_editor_view_camera_copy (lrg_editor_view_state_get_camera (f->vs, 0));

	lrg_editor_viewport_nav_scroll (f->nav, 0.0f, 1.0f, NOMOD);
	g_assert_true (lrg_editor_view_camera_equal (first, lrg_editor_view_state_get_camera (f->vs, 0)));
	g_assert_true (lrg_editor_viewport_nav_get_camera (f->nav) == lrg_editor_view_state_get_camera (f->vs, 1));
}

/* Swallowing: a second button during navigation belongs to it. */
static void
test_nav_one_gesture (Fixture       *f,
                      gconstpointer  data)
{
	lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_RIGHT, CX, CY, NOMOD);
	g_assert_true (lrg_editor_viewport_nav_pointer_press (f->nav, GRL_MOUSE_BUTTON_LEFT, CX, CY, NOMOD));
	g_assert_true (lrg_editor_viewport_nav_pointer_release (f->nav, GRL_MOUSE_BUTTON_LEFT, CX, CY, NOMOD));
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_FLY);
	lrg_editor_viewport_nav_cancel (f->nav);
	g_assert_cmpint (lrg_editor_viewport_nav_get_mode (f->nav), ==, LRG_EDITOR_NAV_MODE_NONE);
	g_assert_cmpuint (f->menus, ==, 0);
	g_assert_cmpuint (f->mode_changes, ==, 2);

	/* A navigator without a view state makes its own. */
	{
		g_autoptr(LrgEditorViewportNav) own = lrg_editor_viewport_nav_new (NULL);

		g_assert_nonnull (lrg_editor_viewport_nav_get_view_state (own));
		g_assert_nonnull (lrg_editor_viewport_nav_get_camera (own));
	}
}

int
main (int   argc,
      char *argv[])
{
	g_test_init (&argc, &argv, NULL);

#define ADD(path, func) \
	g_test_add ("/editor/viewport-nav/" path, Fixture, NULL, fixture_setup, func, fixture_teardown)

	ADD ("right-drag-flies", test_nav_right_drag_flies);
	ADD ("fly-keys", test_nav_fly_keys);
	ADD ("focus-lost", test_nav_focus_lost);
	ADD ("orbit-pan-dolly", test_nav_orbit_pan_dolly);
	ADD ("invert-y", test_nav_invert_y);
	ADD ("schemes", test_nav_schemes);
	ADD ("gizmo", test_nav_gizmo);
	ADD ("box-and-click", test_nav_box_and_click);
	ADD ("2d", test_nav_2d);
	ADD ("active-viewport", test_nav_active_viewport);
	ADD ("one-gesture", test_nav_one_gesture);

#undef ADD

	return g_test_run ();
}
