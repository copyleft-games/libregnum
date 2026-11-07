/* lrg-editor-view-state.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorViewState and LrgEditorViewCamera: the editor's headless view
 * model. See the header for the overview. The camera helpers are moved
 * verbatim (in behaviour) from the old src/editor/ui app:
 *
 *   editor_view_reset          -> lrg_editor_view_camera_reset
 *   editor_view_align          -> lrg_editor_view_camera_align
 *   editor_view_orbit          -> lrg_editor_view_camera_orbit
 *   a_view_persp               -> lrg_editor_view_camera_toggle_ortho
 *   a_toggle_2d                -> lrg_editor_view_camera_set_2d
 *   a_orbit_*, a_zoom_*        -> lrg_editor_view_camera_orbit_step / zoom_step
 *   editor_app_frame_selection -> lrg_editor_view_camera_focus_selection
 *   editor_app_frame_all       -> lrg_editor_view_camera_frame_all
 */

#include "config.h"

#include "lrg-editor-view-state.h"
#include "lrg-editor.h"
#include "lrg-editor-settings.h"
#include "lrg-level.h"
#include "lrg-node.h"
#include <math.h>
#include <string.h>

#define DEFAULT_FOV       (50.0)
#define DEFAULT_FLY_SPEED (8.0)
#define MIN_FLY_SPEED     (0.1)
#define MAX_FLY_SPEED     (500.0)
#define PITCH_LIMIT       ((gfloat) G_PI_2 - 0.0175f)   /* as lrg_editor_view_orbit() */

/* ==========================================================================
 * Enum types (hand-written: the enums live in this header, not lrg-enums.h)
 * ========================================================================== */

GType
lrg_editor_viewport_layout_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE, "LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE", "single" },
			{ LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_H, "LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_H", "split-h" },
			{ LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_V, "LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_V", "split-v" },
			{ LRG_EDITOR_VIEWPORT_LAYOUT_QUAD, "LRG_EDITOR_VIEWPORT_LAYOUT_QUAD", "quad" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorViewportLayout"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_editor_render_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_RENDER_MODE_LIT, "LRG_EDITOR_RENDER_MODE_LIT", "lit" },
			{ LRG_EDITOR_RENDER_MODE_UNLIT, "LRG_EDITOR_RENDER_MODE_UNLIT", "unlit" },
			{ LRG_EDITOR_RENDER_MODE_WIREFRAME, "LRG_EDITOR_RENDER_MODE_WIREFRAME", "wireframe" },
			{ LRG_EDITOR_RENDER_MODE_NORMALS, "LRG_EDITOR_RENDER_MODE_NORMALS", "normals" },
			{ LRG_EDITOR_RENDER_MODE_LIGHTING_ONLY, "LRG_EDITOR_RENDER_MODE_LIGHTING_ONLY", "lighting-only" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorRenderMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_editor_view_overlays_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GFlagsValue values[] = {
			{ LRG_EDITOR_VIEW_OVERLAY_NONE, "LRG_EDITOR_VIEW_OVERLAY_NONE", "none" },
			{ LRG_EDITOR_VIEW_OVERLAY_GRID, "LRG_EDITOR_VIEW_OVERLAY_GRID", "grid" },
			{ LRG_EDITOR_VIEW_OVERLAY_GIZMOS, "LRG_EDITOR_VIEW_OVERLAY_GIZMOS", "gizmos" },
			{ LRG_EDITOR_VIEW_OVERLAY_ICONS, "LRG_EDITOR_VIEW_OVERLAY_ICONS", "icons" },
			{ LRG_EDITOR_VIEW_OVERLAY_BOUNDS, "LRG_EDITOR_VIEW_OVERLAY_BOUNDS", "bounds" },
			{ LRG_EDITOR_VIEW_OVERLAY_STATS, "LRG_EDITOR_VIEW_OVERLAY_STATS", "stats" },
			{ LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING, "LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING", "scene-lighting" },
			{ LRG_EDITOR_VIEW_OVERLAY_SHADOWS, "LRG_EDITOR_VIEW_OVERLAY_SHADOWS", "shadows" },
			{ LRG_EDITOR_VIEW_OVERLAY_SKY, "LRG_EDITOR_VIEW_OVERLAY_SKY", "sky" },
			{ LRG_EDITOR_VIEW_OVERLAY_POST, "LRG_EDITOR_VIEW_OVERLAY_POST", "post" },
			{ 0, NULL, NULL }
		};
		GType id = g_flags_register_static (g_intern_static_string ("LrgEditorViewOverlays"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_editor_nav_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_NAV_MODE_NONE, "LRG_EDITOR_NAV_MODE_NONE", "none" },
			{ LRG_EDITOR_NAV_MODE_FLY, "LRG_EDITOR_NAV_MODE_FLY", "fly" },
			{ LRG_EDITOR_NAV_MODE_ORBIT, "LRG_EDITOR_NAV_MODE_ORBIT", "orbit" },
			{ LRG_EDITOR_NAV_MODE_PAN, "LRG_EDITOR_NAV_MODE_PAN", "pan" },
			{ LRG_EDITOR_NAV_MODE_BOX, "LRG_EDITOR_NAV_MODE_BOX", "box" },
			{ LRG_EDITOR_NAV_MODE_ZOOM, "LRG_EDITOR_NAV_MODE_ZOOM", "zoom" },
			{ LRG_EDITOR_NAV_MODE_GIZMO, "LRG_EDITOR_NAV_MODE_GIZMO", "gizmo" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorNavMode"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_editor_play_state_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_PLAY_STATE_STOPPED, "LRG_EDITOR_PLAY_STATE_STOPPED", "stopped" },
			{ LRG_EDITOR_PLAY_STATE_PLAYING, "LRG_EDITOR_PLAY_STATE_PLAYING", "playing" },
			{ LRG_EDITOR_PLAY_STATE_PAUSED, "LRG_EDITOR_PLAY_STATE_PAUSED", "paused" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorPlayState"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_editor_axis_view_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_AXIS_VIEW_NONE, "LRG_EDITOR_AXIS_VIEW_NONE", "none" },
			{ LRG_EDITOR_AXIS_VIEW_FRONT, "LRG_EDITOR_AXIS_VIEW_FRONT", "front" },
			{ LRG_EDITOR_AXIS_VIEW_BACK, "LRG_EDITOR_AXIS_VIEW_BACK", "back" },
			{ LRG_EDITOR_AXIS_VIEW_RIGHT, "LRG_EDITOR_AXIS_VIEW_RIGHT", "right" },
			{ LRG_EDITOR_AXIS_VIEW_LEFT, "LRG_EDITOR_AXIS_VIEW_LEFT", "left" },
			{ LRG_EDITOR_AXIS_VIEW_TOP, "LRG_EDITOR_AXIS_VIEW_TOP", "top" },
			{ LRG_EDITOR_AXIS_VIEW_BOTTOM, "LRG_EDITOR_AXIS_VIEW_BOTTOM", "bottom" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorAxisView"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

GType
lrg_editor_tile_tool_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_EDITOR_TILE_TOOL_BRUSH, "LRG_EDITOR_TILE_TOOL_BRUSH", "brush" },
			{ LRG_EDITOR_TILE_TOOL_ERASER, "LRG_EDITOR_TILE_TOOL_ERASER", "eraser" },
			{ LRG_EDITOR_TILE_TOOL_RECT, "LRG_EDITOR_TILE_TOOL_RECT", "rect" },
			{ LRG_EDITOR_TILE_TOOL_FILL, "LRG_EDITOR_TILE_TOOL_FILL", "fill" },
			{ LRG_EDITOR_TILE_TOOL_PICK, "LRG_EDITOR_TILE_TOOL_PICK", "pick" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgEditorTileTool"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType) type_id;
}

/* The class of a static enum type. The first call takes one reference
 * that is kept for the life of the process (static types are never
 * finalized), later calls only peek, so the nick strings stay valid. */
static GEnumClass *
enum_class (GType type)
{
	gpointer klass = g_type_class_peek (type);

	if (klass == NULL)
		klass = g_type_class_ref (type);
	return (GEnumClass *) klass;
}

/* Nick of an enum value, or "?". */
static const gchar *
enum_nick (GType type,
           gint  value)
{
	GEnumValue *ev = g_enum_get_value (enum_class (type), value);

	return ev != NULL ? ev->value_nick : "?";
}

static gboolean
enum_from_nick (GType        type,
                const gchar *nick,
                gint        *out)
{
	GEnumValue *ev;

	if (nick == NULL)
		return FALSE;
	ev = g_enum_get_value_by_nick (enum_class (type), nick);
	if (ev == NULL)
		return FALSE;
	*out = ev->value;
	return TRUE;
}

const gchar *
lrg_editor_nav_mode_to_string (LrgEditorNavMode mode)
{
	return enum_nick (LRG_TYPE_EDITOR_NAV_MODE, (gint) mode);
}

const gchar *
lrg_editor_tile_tool_to_string (LrgEditorTileTool tool)
{
	return enum_nick (LRG_TYPE_EDITOR_TILE_TOOL, (gint) tool);
}

gboolean
lrg_editor_tile_tool_from_string (const gchar       *name,
                                  LrgEditorTileTool *tool)
{
	gint v = 0;

	g_return_val_if_fail (tool != NULL, FALSE);

	if (!enum_from_nick (LRG_TYPE_EDITOR_TILE_TOOL, name, &v))
		return FALSE;
	*tool = (LrgEditorTileTool) v;
	return TRUE;
}

/* ==========================================================================
 * LrgEditorViewCamera
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgEditorViewCamera, lrg_editor_view_camera,
                     lrg_editor_view_camera_copy, lrg_editor_view_camera_free)

/* Direction (target -> camera) of each axis view, indexed by the enum. */
static const gfloat axis_dirs[7][3] = {
	{ 0.0f, 0.0f, 0.0f },    /* NONE */
	{ 0.0f, 0.0f, 1.0f },    /* FRONT */
	{ 0.0f, 0.0f, -1.0f },   /* BACK */
	{ 1.0f, 0.0f, 0.0f },    /* RIGHT */
	{ -1.0f, 0.0f, 0.0f },   /* LEFT */
	{ 0.0f, 1.0f, 0.0f },    /* TOP */
	{ 0.0f, -1.0f, 0.0f }    /* BOTTOM */
};

/* Which axis view (if any) an exact unit direction is. */
static LrgEditorAxisView
axis_for_direction (gfloat x,
                    gfloat y,
                    gfloat z)
{
	gint i;

	for (i = 1; i < (gint) G_N_ELEMENTS (axis_dirs); i++)
		if (axis_dirs[i][0] == x && axis_dirs[i][1] == y && axis_dirs[i][2] == z)
			return (LrgEditorAxisView) i;
	return LRG_EDITOR_AXIS_VIEW_NONE;
}

/* Whether the camera still looks exactly along @axis (position - target
 * has components only on that axis, with the right sign). */
static gboolean
still_on_axis (const LrgEditorView *view,
               LrgEditorAxisView    axis)
{
	gfloat d[3];
	gint   i;

	if (axis == LRG_EDITOR_AXIS_VIEW_NONE)
		return FALSE;
	for (i = 0; i < 3; i++)
	{
		d[i] = view->position[i] - view->target[i];
		if (axis_dirs[axis][i] == 0.0f && d[i] != 0.0f)
			return FALSE;
		if (axis_dirs[axis][i] != 0.0f && d[i] * axis_dirs[axis][i] <= 0.0f)
			return FALSE;
	}
	return TRUE;
}

LrgEditorViewCamera *
lrg_editor_view_camera_new (void)
{
	LrgEditorViewCamera *camera = g_new0 (LrgEditorViewCamera, 1);

	lrg_editor_view_init_default (&camera->view, 1.0f, 1.0f);
	lrg_editor_view_camera_reset (camera, (gfloat) DEFAULT_FOV);
	return camera;
}

LrgEditorViewCamera *
lrg_editor_view_camera_copy (const LrgEditorViewCamera *camera)
{
	g_return_val_if_fail (camera != NULL, NULL);

	return g_memdup2 (camera, sizeof *camera);
}

void
lrg_editor_view_camera_free (LrgEditorViewCamera *camera)
{
	g_free (camera);
}

gboolean
lrg_editor_view_camera_equal (const LrgEditorViewCamera *a,
                              const LrgEditorViewCamera *b)
{
	const LrgEditorView *va, *vb;

	g_return_val_if_fail (a != NULL && b != NULL, FALSE);

	/* Field by field: the struct may have padding. */
	va = &a->view;
	vb = &b->view;
	return memcmp (va->position, vb->position, sizeof va->position) == 0 &&
	       memcmp (va->target, vb->target, sizeof va->target) == 0 &&
	       memcmp (va->up, vb->up, sizeof va->up) == 0 &&
	       va->fovy == vb->fovy && va->ortho_height == vb->ortho_height &&
	       (!va->orthographic) == (!vb->orthographic) &&
	       va->near_plane == vb->near_plane && va->far_plane == vb->far_plane &&
	       va->width == vb->width && va->height == vb->height &&
	       (!a->view_2d) == (!b->view_2d) && (!a->ortho_from_align) == (!b->ortho_from_align) &&
	       a->axis_view == b->axis_view;
}

void
lrg_editor_view_camera_reset (LrgEditorViewCamera *camera,
                              gfloat               fovy)
{
	gfloat w, h;

	g_return_if_fail (camera != NULL);

	/* The old editor_view_reset(): keep the size, start from the default
	 * perspective view with the configured field of view. */
	w = MAX (camera->view.width, 1.0f);
	h = MAX (camera->view.height, 1.0f);
	lrg_editor_view_init_default (&camera->view, w, h);
	camera->view.fovy = fovy;
	camera->ortho_from_align = FALSE;
	camera->axis_view = LRG_EDITOR_AXIS_VIEW_NONE;
	if (camera->view_2d)
	{
		/* Looking down -Z at the XY plane. */
		camera->view.position[0] = 0.0f;
		camera->view.position[1] = 0.0f;
		camera->view.position[2] = 50.0f;
		camera->view.orthographic = TRUE;
		camera->view.ortho_height = 20.0f;
	}
}

void
lrg_editor_view_camera_set_size (LrgEditorViewCamera *camera,
                                 gfloat               width,
                                 gfloat               height)
{
	g_return_if_fail (camera != NULL);

	camera->view.width = MAX (width, 1.0f);
	camera->view.height = MAX (height, 1.0f);
}

void
lrg_editor_view_camera_to_view (const LrgEditorViewCamera *camera,
                                LrgEditorView             *out)
{
	g_return_if_fail (camera != NULL);
	g_return_if_fail (out != NULL);

	*out = camera->view;
}

void
lrg_editor_view_camera_from_view (LrgEditorViewCamera *camera,
                                  const LrgEditorView *view)
{
	g_return_if_fail (camera != NULL);
	g_return_if_fail (view != NULL);

	camera->view = *view;
	if (!still_on_axis (&camera->view, camera->axis_view))
		camera->axis_view = LRG_EDITOR_AXIS_VIEW_NONE;
}

gfloat
lrg_editor_view_camera_get_yaw (const LrgEditorViewCamera *camera)
{
	g_return_val_if_fail (camera != NULL, 0.0f);

	/* Same spherical convention as lrg_editor_view_orbit(). */
	return atan2f (camera->view.position[0] - camera->view.target[0],
	               camera->view.position[2] - camera->view.target[2]);
}

gfloat
lrg_editor_view_camera_get_pitch (const LrgEditorViewCamera *camera)
{
	gfloat dist;

	g_return_val_if_fail (camera != NULL, 0.0f);

	dist = lrg_editor_view_camera_get_distance (camera);
	if (dist < 1e-6f)
		return 0.0f;
	return asinf (CLAMP ((camera->view.position[1] - camera->view.target[1]) / dist, -1.0f, 1.0f));
}

gfloat
lrg_editor_view_camera_get_distance (const LrgEditorViewCamera *camera)
{
	gfloat dx, dy, dz;

	g_return_val_if_fail (camera != NULL, 0.0f);

	dx = camera->view.position[0] - camera->view.target[0];
	dy = camera->view.position[1] - camera->view.target[1];
	dz = camera->view.position[2] - camera->view.target[2];
	return sqrtf (dx * dx + dy * dy + dz * dz);
}

void
lrg_editor_view_camera_set_orbit (LrgEditorViewCamera *camera,
                                  gfloat               yaw,
                                  gfloat               pitch,
                                  gfloat               distance)
{
	LrgEditorView *v;

	g_return_if_fail (camera != NULL);

	v = &camera->view;
	pitch = CLAMP (pitch, -PITCH_LIMIT, PITCH_LIMIT);
	distance = MAX (distance, 0.05f);
	v->position[0] = v->target[0] + distance * cosf (pitch) * sinf (yaw);
	v->position[1] = v->target[1] + distance * sinf (pitch);
	v->position[2] = v->target[2] + distance * cosf (pitch) * cosf (yaw);
	v->up[0] = 0.0f;
	v->up[1] = 1.0f;
	v->up[2] = 0.0f;
	camera->axis_view = LRG_EDITOR_AXIS_VIEW_NONE;
}

void
lrg_editor_view_camera_align (LrgEditorViewCamera *camera,
                              gfloat               x,
                              gfloat               y,
                              gfloat               z)
{
	const gfloat dir[3] = { x, y, z };

	g_return_if_fail (camera != NULL);

	/* Camera on an axis, orthographic; the next orbit returns to
	 * perspective (Blender's auto-perspective). */
	lrg_editor_view_align (&camera->view, dir);
	camera->axis_view = axis_for_direction (x, y, z);
	if (!camera->view_2d)
	{
		camera->view.orthographic = TRUE;
		camera->ortho_from_align = TRUE;
	}
}

void
lrg_editor_view_camera_set_axis_view (LrgEditorViewCamera *camera,
                                      LrgEditorAxisView    axis)
{
	g_return_if_fail (camera != NULL);

	if (axis <= LRG_EDITOR_AXIS_VIEW_NONE || axis > LRG_EDITOR_AXIS_VIEW_BOTTOM)
		return;
	lrg_editor_view_camera_align (camera, axis_dirs[axis][0], axis_dirs[axis][1], axis_dirs[axis][2]);
}

void
lrg_editor_view_camera_orbit (LrgEditorViewCamera *camera,
                              gfloat               yaw,
                              gfloat               pitch)
{
	g_return_if_fail (camera != NULL);

	/* Orbits around the target; leaves an aligned ortho view. */
	if (camera->view_2d)
		return;
	if (camera->ortho_from_align)
	{
		camera->view.orthographic = FALSE;
		camera->ortho_from_align = FALSE;
	}
	lrg_editor_view_orbit (&camera->view, yaw, pitch);
	if (yaw != 0.0f || pitch != 0.0f)
		camera->axis_view = LRG_EDITOR_AXIS_VIEW_NONE;
}

void
lrg_editor_view_camera_orbit_step (LrgEditorViewCamera *camera,
                                   gint                 right,
                                   gint                 down)
{
	g_return_if_fail (camera != NULL);

	/* KP_4 (+yaw) / KP_6 (-yaw) / KP_8 (-pitch) / KP_2 (+pitch). */
	lrg_editor_view_camera_orbit (camera, (gfloat) -right * LRG_EDITOR_VIEW_ORBIT_STEP,
	                              (gfloat) down * LRG_EDITOR_VIEW_ORBIT_STEP);
}

void
lrg_editor_view_camera_fly (LrgEditorViewCamera *camera,
                            gfloat               forward,
                            gfloat               right,
                            gfloat               up,
                            gfloat               yaw,
                            gfloat               pitch)
{
	g_return_if_fail (camera != NULL);

	/* As before: looking or moving in the view plane leaves an axis view
	 * for perspective; a pure vertical (Q/E) move keeps it. */
	if (camera->ortho_from_align && (yaw != 0.0f || pitch != 0.0f || forward != 0.0f || right != 0.0f))
	{
		camera->view.orthographic = FALSE;
		camera->ortho_from_align = FALSE;
	}
	lrg_editor_view_fly (&camera->view, forward, right, up, yaw, pitch);
	if (!still_on_axis (&camera->view, camera->axis_view))
		camera->axis_view = LRG_EDITOR_AXIS_VIEW_NONE;
}

void
lrg_editor_view_camera_pan (LrgEditorViewCamera *camera,
                            gfloat               dx,
                            gfloat               dy)
{
	g_return_if_fail (camera != NULL);

	/* Panning keeps the direction, so the axis view survives. */
	lrg_editor_view_pan (&camera->view, dx, dy);
}

void
lrg_editor_view_camera_dolly (LrgEditorViewCamera *camera,
                              gfloat               factor)
{
	g_return_if_fail (camera != NULL);

	lrg_editor_view_dolly (&camera->view, factor);
}

void
lrg_editor_view_camera_zoom_step (LrgEditorViewCamera *camera,
                                  gboolean             zoom_in)
{
	g_return_if_fail (camera != NULL);

	lrg_editor_view_dolly (&camera->view, zoom_in ? 0.8f : 1.25f);
}

void
lrg_editor_view_camera_toggle_ortho (LrgEditorViewCamera *camera)
{
	g_return_if_fail (camera != NULL);

	if (camera->view_2d)
		return;
	camera->view.orthographic = !camera->view.orthographic;
	camera->ortho_from_align = FALSE;
}

void
lrg_editor_view_camera_set_2d (LrgEditorViewCamera *camera,
                               gboolean             view_2d,
                               gfloat               fovy)
{
	g_return_if_fail (camera != NULL);

	camera->view_2d = view_2d ? TRUE : FALSE;
	lrg_editor_view_camera_reset (camera, fovy);
}

void
lrg_editor_view_camera_frame (LrgEditorViewCamera *camera,
                              const gfloat        *center,
                              gfloat               radius)
{
	g_return_if_fail (camera != NULL);
	g_return_if_fail (center != NULL);

	lrg_editor_view_frame (&camera->view, center, radius);
}

gboolean
lrg_editor_view_camera_frame_nodes (LrgEditorViewCamera *camera,
                                    GPtrArray           *nodes)
{
	gfloat center[3], radius;

	g_return_val_if_fail (camera != NULL, FALSE);

	if (nodes == NULL || !lrg_editor_compute_bounds (nodes, center, &radius))
		return FALSE;
	lrg_editor_view_frame (&camera->view, center, radius);
	return TRUE;
}

void
lrg_editor_view_camera_frame_all (LrgEditorViewCamera *camera,
                                  LrgLevel            *level,
                                  gfloat               fovy)
{
	GPtrArray *top;

	g_return_if_fail (camera != NULL);
	g_return_if_fail (LRG_IS_LEVEL (level));

	/* Fit the top-level nodes; an empty level resets the camera. */
	top = lrg_node_get_children (lrg_level_get_root (level));
	if (!lrg_editor_view_camera_frame_nodes (camera, top))
		lrg_editor_view_camera_reset (camera, fovy);
}

void
lrg_editor_view_camera_focus_selection (LrgEditorViewCamera *camera,
                                        LrgEditor           *editor,
                                        gfloat               fovy)
{
	g_autoptr(GPtrArray) roots = NULL;

	g_return_if_fail (camera != NULL);
	g_return_if_fail (LRG_IS_EDITOR (editor));

	roots = lrg_editor_get_selected_roots (editor);
	if (roots->len == 0)
	{
		lrg_editor_view_camera_frame_all (camera, lrg_editor_get_level (editor), fovy);
		return;
	}
	lrg_editor_view_camera_frame_nodes (camera, roots);
}

/* --- String form ------------------------------------------------------------ */

#define CAMERA_FIELDS (12)

gchar *
lrg_editor_view_camera_to_string (const LrgEditorViewCamera *camera)
{
	const LrgEditorView *v;
	gdouble              f[CAMERA_FIELDS];
	GString             *s;
	gint                 i;

	g_return_val_if_fail (camera != NULL, NULL);

	v = &camera->view;
	f[0] = v->position[0]; f[1] = v->position[1]; f[2] = v->position[2];
	f[3] = v->target[0];   f[4] = v->target[1];   f[5] = v->target[2];
	f[6] = v->fovy;
	f[7] = v->ortho_height;
	f[8] = v->orthographic ? 1.0 : 0.0;
	f[9] = camera->view_2d ? 1.0 : 0.0;
	f[10] = (gdouble) camera->axis_view;
	f[11] = camera->ortho_from_align ? 1.0 : 0.0;

	/* %.9g round-trips a float exactly; g_ascii_formatd ignores the
	 * locale's decimal separator. */
	s = g_string_new (NULL);
	for (i = 0; i < CAMERA_FIELDS; i++)
	{
		gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

		if (i > 0)
			g_string_append_c (s, ',');
		g_string_append (s, g_ascii_formatd (buf, sizeof buf, "%.9g", f[i]));
	}
	return g_string_free (s, FALSE);
}

gboolean
lrg_editor_view_camera_from_string (LrgEditorViewCamera *camera,
                                    const gchar         *text)
{
	g_auto(GStrv) parts = NULL;
	gdouble       f[CAMERA_FIELDS];
	gint          i, axis;

	g_return_val_if_fail (camera != NULL, FALSE);

	if (text == NULL)
		return FALSE;
	parts = g_strsplit (text, ",", -1);
	if (g_strv_length (parts) != CAMERA_FIELDS)
		return FALSE;

	/* Parse everything before touching the camera. */
	for (i = 0; i < CAMERA_FIELDS; i++)
	{
		gchar *end = NULL;

		f[i] = g_ascii_strtod (g_strstrip (parts[i]), &end);
		if (end == parts[i] || *end != '\0' || !isfinite (f[i]))
			return FALSE;
	}
	axis = (gint) f[10];
	if (axis < LRG_EDITOR_AXIS_VIEW_NONE || axis > LRG_EDITOR_AXIS_VIEW_BOTTOM)
		return FALSE;

	camera->view.position[0] = (gfloat) f[0];
	camera->view.position[1] = (gfloat) f[1];
	camera->view.position[2] = (gfloat) f[2];
	camera->view.target[0] = (gfloat) f[3];
	camera->view.target[1] = (gfloat) f[4];
	camera->view.target[2] = (gfloat) f[5];
	camera->view.up[0] = 0.0f;
	camera->view.up[1] = 1.0f;
	camera->view.up[2] = 0.0f;
	camera->view.fovy = CLAMP ((gfloat) f[6], 10.0f, 150.0f);
	camera->view.ortho_height = MAX ((gfloat) f[7], 0.01f);
	camera->view.orthographic = f[8] != 0.0;
	camera->view_2d = f[9] != 0.0;
	camera->axis_view = (LrgEditorAxisView) axis;
	camera->ortho_from_align = f[11] != 0.0;
	return TRUE;
}

/* ==========================================================================
 * LrgEditorViewState
 * ========================================================================== */

typedef struct
{
	LrgEditorViewCamera   camera;
	LrgEditorRenderMode   render_mode;
	LrgEditorViewOverlays overlays;
} Viewport;

struct _LrgEditorViewState
{
	GObject                 parent_instance;

	LrgEditor              *editor;     /* strong ref, nullable */
	Viewport                viewports[LRG_EDITOR_VIEW_MAX_VIEWPORTS];
	LrgEditorViewportLayout layout;
	guint                   active;
	gdouble                 fov;
	gdouble                 fly_speed;

	LrgEditorNavMode        nav_mode;
	gchar                  *dialog;
	gboolean                palette_open;
	gchar                  *timeline_name;
	gfloat                  timeline_time;
	gboolean                timeline_playing;
	gboolean                timeline_previewing;
	gchar                  *inspector_lock;
	gchar                  *status;
	LrgEditorPlayState      play_state;
	LrgEditorTileTool       tile_tool;
	gboolean                tile_mode;
};

G_DEFINE_FINAL_TYPE (LrgEditorViewState, lrg_editor_view_state, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_EDITOR,
	PROP_LAYOUT,
	PROP_LAYOUT_NAME,
	PROP_VIEWPORT_COUNT,
	PROP_ACTIVE_VIEWPORT,
	PROP_CAMERA,
	PROP_RENDER_MODE,
	PROP_OVERLAYS,
	PROP_FOV,
	PROP_FLY_SPEED,
	PROP_NAV_MODE,
	PROP_DIALOG,
	PROP_PALETTE_OPEN,
	PROP_TIMELINE_NAME,
	PROP_TIMELINE_TIME,
	PROP_TIMELINE_PLAYING,
	PROP_TIMELINE_PREVIEWING,
	PROP_INSPECTOR_LOCK,
	PROP_STATUS,
	PROP_PLAY_STATE,
	PROP_TILE_TOOL,
	PROP_TILE_MODE,
	N_PROPS
};

enum
{
	SIGNAL_CAMERA_CHANGED,
	SIGNAL_VIEWPORT_CHANGED,
	N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint       signals[N_SIGNALS];

/* Resolves -1 to the active viewport and clamps to the array. */
static guint
resolve_index (LrgEditorViewState *self,
               gint                index)
{
	if (index < 0)
		return self->active;
	return MIN ((guint) index, (guint) LRG_EDITOR_VIEW_MAX_VIEWPORTS - 1);
}

static guint
layout_count (LrgEditorViewportLayout layout)
{
	switch (layout)
	{
	case LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_H:
	case LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_V:
		return 2;
	case LRG_EDITOR_VIEWPORT_LAYOUT_QUAD:
		return 4;
	case LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE:
	default:
		return 1;
	}
}

/* Emits camera-changed for @index (and notify::camera when it is the
 * active viewport). */
static void
emit_camera_changed (LrgEditorViewState *self,
                     guint               index)
{
	g_signal_emit (self, signals[SIGNAL_CAMERA_CHANGED], 0, index);
	if (index == self->active)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAMERA]);
}

/* Notifies the properties that mirror the active viewport. */
static void
notify_active_viewport (LrgEditorViewState *self)
{
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAMERA]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RENDER_MODE]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OVERLAYS]);
}

static void
set_string (LrgEditorViewState  *self,
            gchar              **field,
            const gchar         *value,
            guint                prop)
{
	if (g_strcmp0 (*field, value) == 0)
		return;
	g_free (*field);
	*field = g_strdup (value);
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
set_bool (LrgEditorViewState *self,
          gboolean           *field,
          gboolean            value,
          guint               prop)
{
	value = value ? TRUE : FALSE;
	if (*field == value)
		return;
	*field = value;
	g_object_notify_by_pspec (G_OBJECT (self), properties[prop]);
}

static void
lrg_editor_view_state_dispose (GObject *object)
{
	LrgEditorViewState *self = LRG_EDITOR_VIEW_STATE (object);

	g_clear_object (&self->editor);

	G_OBJECT_CLASS (lrg_editor_view_state_parent_class)->dispose (object);
}

static void
lrg_editor_view_state_finalize (GObject *object)
{
	LrgEditorViewState *self = LRG_EDITOR_VIEW_STATE (object);

	g_free (self->dialog);
	g_free (self->timeline_name);
	g_free (self->inspector_lock);
	g_free (self->status);

	G_OBJECT_CLASS (lrg_editor_view_state_parent_class)->finalize (object);
}

static void
lrg_editor_view_state_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	LrgEditorViewState *self = LRG_EDITOR_VIEW_STATE (object);

	switch (prop_id)
	{
	case PROP_EDITOR:              g_value_set_object (value, self->editor); break;
	case PROP_LAYOUT:              g_value_set_enum (value, (gint) self->layout); break;
	case PROP_LAYOUT_NAME:         g_value_set_string (value, lrg_editor_view_state_get_layout_name (self)); break;
	case PROP_VIEWPORT_COUNT:      g_value_set_uint (value, layout_count (self->layout)); break;
	case PROP_ACTIVE_VIEWPORT:     g_value_set_uint (value, self->active); break;
	case PROP_CAMERA:              g_value_set_boxed (value, &self->viewports[self->active].camera); break;
	case PROP_RENDER_MODE:         g_value_set_enum (value, (gint) self->viewports[self->active].render_mode); break;
	case PROP_OVERLAYS:            g_value_set_flags (value, (guint) self->viewports[self->active].overlays); break;
	case PROP_FOV:                 g_value_set_double (value, self->fov); break;
	case PROP_FLY_SPEED:           g_value_set_double (value, self->fly_speed); break;
	case PROP_NAV_MODE:            g_value_set_enum (value, (gint) self->nav_mode); break;
	case PROP_DIALOG:              g_value_set_string (value, self->dialog); break;
	case PROP_PALETTE_OPEN:        g_value_set_boolean (value, self->palette_open); break;
	case PROP_TIMELINE_NAME:       g_value_set_string (value, self->timeline_name); break;
	case PROP_TIMELINE_TIME:       g_value_set_float (value, self->timeline_time); break;
	case PROP_TIMELINE_PLAYING:    g_value_set_boolean (value, self->timeline_playing); break;
	case PROP_TIMELINE_PREVIEWING: g_value_set_boolean (value, self->timeline_previewing); break;
	case PROP_INSPECTOR_LOCK:      g_value_set_string (value, self->inspector_lock); break;
	case PROP_STATUS:              g_value_set_string (value, self->status); break;
	case PROP_PLAY_STATE:          g_value_set_enum (value, (gint) self->play_state); break;
	case PROP_TILE_TOOL:           g_value_set_enum (value, (gint) self->tile_tool); break;
	case PROP_TILE_MODE:           g_value_set_boolean (value, self->tile_mode); break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_view_state_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	LrgEditorViewState *self = LRG_EDITOR_VIEW_STATE (object);

	switch (prop_id)
	{
	case PROP_EDITOR:
		g_set_object (&self->editor, g_value_get_object (value));
		break;
	case PROP_LAYOUT:
		lrg_editor_view_state_set_layout (self, (LrgEditorViewportLayout) g_value_get_enum (value));
		break;
	case PROP_LAYOUT_NAME:
		lrg_editor_view_state_set_layout_name (self, g_value_get_string (value));
		break;
	case PROP_ACTIVE_VIEWPORT:
		lrg_editor_view_state_set_active_viewport (self, g_value_get_uint (value));
		break;
	case PROP_CAMERA:
		if (g_value_get_boxed (value) != NULL)
			lrg_editor_view_state_set_camera (self, -1, g_value_get_boxed (value));
		break;
	case PROP_RENDER_MODE:
		lrg_editor_view_state_set_render_mode (self, -1, (LrgEditorRenderMode) g_value_get_enum (value));
		break;
	case PROP_OVERLAYS:
		lrg_editor_view_state_set_overlays (self, -1, (LrgEditorViewOverlays) g_value_get_flags (value));
		break;
	case PROP_FOV:
		lrg_editor_view_state_set_fov (self, g_value_get_double (value));
		break;
	case PROP_FLY_SPEED:
		lrg_editor_view_state_set_fly_speed (self, g_value_get_double (value));
		break;
	case PROP_NAV_MODE:
		lrg_editor_view_state_set_nav_mode (self, (LrgEditorNavMode) g_value_get_enum (value));
		break;
	case PROP_DIALOG:
		lrg_editor_view_state_set_dialog (self, g_value_get_string (value));
		break;
	case PROP_PALETTE_OPEN:
		lrg_editor_view_state_set_palette_open (self, g_value_get_boolean (value));
		break;
	case PROP_TIMELINE_NAME:
		lrg_editor_view_state_set_timeline_name (self, g_value_get_string (value));
		break;
	case PROP_TIMELINE_TIME:
		lrg_editor_view_state_set_timeline_time (self, g_value_get_float (value));
		break;
	case PROP_TIMELINE_PLAYING:
		lrg_editor_view_state_set_timeline_playing (self, g_value_get_boolean (value));
		break;
	case PROP_TIMELINE_PREVIEWING:
		lrg_editor_view_state_set_timeline_previewing (self, g_value_get_boolean (value));
		break;
	case PROP_INSPECTOR_LOCK:
		lrg_editor_view_state_set_inspector_lock (self, g_value_get_string (value));
		break;
	case PROP_STATUS:
		lrg_editor_view_state_set_status (self, g_value_get_string (value));
		break;
	case PROP_PLAY_STATE:
		lrg_editor_view_state_set_play_state (self, (LrgEditorPlayState) g_value_get_enum (value));
		break;
	case PROP_TILE_TOOL:
		lrg_editor_view_state_set_tile_tool (self, (LrgEditorTileTool) g_value_get_enum (value));
		break;
	case PROP_TILE_MODE:
		lrg_editor_view_state_set_tile_mode (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_editor_view_state_class_init (LrgEditorViewStateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamFlags   rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;
	GParamFlags   ro = G_PARAM_READABLE | G_PARAM_STATIC_STRINGS;

	object_class->dispose = lrg_editor_view_state_dispose;
	object_class->finalize = lrg_editor_view_state_finalize;
	object_class->get_property = lrg_editor_view_state_get_property;
	object_class->set_property = lrg_editor_view_state_set_property;

	/**
	 * LrgEditorViewState:editor:
	 *
	 * The editor whose level and selection the view commands act on. It
	 * owns the transform tool, snapping and transform space.
	 */
	properties[PROP_EDITOR] =
		g_param_spec_object ("editor", "Editor", "The editor the view acts on",
		                     LRG_TYPE_EDITOR,
		                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgEditorViewState:layout:
	 *
	 * How the viewport panel is divided.
	 */
	properties[PROP_LAYOUT] =
		g_param_spec_enum ("layout", "Layout", "Viewport layout",
		                   LRG_TYPE_EDITOR_VIEWPORT_LAYOUT, LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE, rw);

	/**
	 * LrgEditorViewState:layout-name:
	 *
	 * The layout as its nick ("single", "split-h", "split-v", "quad"), for
	 * binding to a string setting. Unknown names are ignored.
	 */
	properties[PROP_LAYOUT_NAME] =
		g_param_spec_string ("layout-name", "Layout Name", "Viewport layout nick", "single", rw);

	/**
	 * LrgEditorViewState:viewport-count:
	 *
	 * How many viewports the layout shows (1, 2 or 4).
	 */
	properties[PROP_VIEWPORT_COUNT] =
		g_param_spec_uint ("viewport-count", "Viewport Count", "Viewports in the layout",
		                   1, LRG_EDITOR_VIEW_MAX_VIEWPORTS, 1, ro);

	/**
	 * LrgEditorViewState:active-viewport:
	 *
	 * The viewport keyboard and IPC view commands act on.
	 */
	properties[PROP_ACTIVE_VIEWPORT] =
		g_param_spec_uint ("active-viewport", "Active Viewport", "Index of the active viewport",
		                   0, LRG_EDITOR_VIEW_MAX_VIEWPORTS - 1, 0, rw);

	/**
	 * LrgEditorViewState:camera:
	 *
	 * The active viewport's camera (a copy when read; setting it copies
	 * the pose and projection, not the viewport size).
	 */
	properties[PROP_CAMERA] =
		g_param_spec_boxed ("camera", "Camera", "Camera of the active viewport",
		                    LRG_TYPE_EDITOR_VIEW_CAMERA, rw);

	/**
	 * LrgEditorViewState:render-mode:
	 *
	 * The active viewport's render mode.
	 */
	properties[PROP_RENDER_MODE] =
		g_param_spec_enum ("render-mode", "Render Mode", "Render mode of the active viewport",
		                   LRG_TYPE_EDITOR_RENDER_MODE, LRG_EDITOR_RENDER_MODE_LIT, rw);

	/**
	 * LrgEditorViewState:overlays:
	 *
	 * The active viewport's overlays.
	 */
	properties[PROP_OVERLAYS] =
		g_param_spec_flags ("overlays", "Overlays", "Overlays of the active viewport",
		                    LRG_TYPE_EDITOR_VIEW_OVERLAYS, LRG_EDITOR_VIEW_OVERLAYS_DEFAULT, rw);

	/**
	 * LrgEditorViewState:fov:
	 *
	 * The field of view (degrees) cameras reset to; the camera-fov
	 * setting.
	 */
	properties[PROP_FOV] =
		g_param_spec_double ("fov", "FOV", "Reset field of view in degrees",
		                     10.0, 150.0, DEFAULT_FOV, rw);

	/**
	 * LrgEditorViewState:fly-speed:
	 *
	 * Fly navigation speed in world units per second. Starts from the
	 * camera-fly-speed setting; the wheel changes it while flying.
	 */
	properties[PROP_FLY_SPEED] =
		g_param_spec_double ("fly-speed", "Fly Speed", "Fly speed in units per second",
		                     MIN_FLY_SPEED, MAX_FLY_SPEED, DEFAULT_FLY_SPEED, rw);

	/**
	 * LrgEditorViewState:nav-mode:
	 *
	 * The navigation in progress (ui.state "nav"), written by
	 * #LrgEditorViewportNav.
	 */
	properties[PROP_NAV_MODE] =
		g_param_spec_enum ("nav-mode", "Nav Mode", "Viewport navigation in progress",
		                   LRG_TYPE_EDITOR_NAV_MODE, LRG_EDITOR_NAV_MODE_NONE, rw);

	/**
	 * LrgEditorViewState:dialog:
	 *
	 * Id of the open modal dialog (ui.state "dialog"), or %NULL.
	 */
	properties[PROP_DIALOG] =
		g_param_spec_string ("dialog", "Dialog", "Open dialog id", NULL, rw);

	/**
	 * LrgEditorViewState:palette-open:
	 *
	 * Whether the command palette or quick open shows (ui.state "palette").
	 */
	properties[PROP_PALETTE_OPEN] =
		g_param_spec_boolean ("palette-open", "Palette Open", "Command palette showing", FALSE, rw);

	/**
	 * LrgEditorViewState:timeline-name:
	 *
	 * The timeline the timeline panel shows (timeline.state "name").
	 */
	properties[PROP_TIMELINE_NAME] =
		g_param_spec_string ("timeline-name", "Timeline Name", "Timeline shown in the panel", NULL, rw);

	/**
	 * LrgEditorViewState:timeline-time:
	 *
	 * The timeline playhead in seconds (timeline.state "time").
	 */
	properties[PROP_TIMELINE_TIME] =
		g_param_spec_float ("timeline-time", "Timeline Time", "Playhead in seconds",
		                    0.0f, G_MAXFLOAT, 0.0f, rw);

	/**
	 * LrgEditorViewState:timeline-playing:
	 *
	 * Whether the timeline plays in the editor (timeline.state "playing").
	 */
	properties[PROP_TIMELINE_PLAYING] =
		g_param_spec_boolean ("timeline-playing", "Timeline Playing", "Timeline playback", FALSE, rw);

	/**
	 * LrgEditorViewState:timeline-previewing:
	 *
	 * Whether nodes show sampled timeline values (timeline.state
	 * "previewing").
	 */
	properties[PROP_TIMELINE_PREVIEWING] =
		g_param_spec_boolean ("timeline-previewing", "Timeline Previewing", "Nodes show sampled values",
		                      FALSE, rw);

	/**
	 * LrgEditorViewState:inspector-lock:
	 *
	 * GUID of the node the inspector is locked to, or %NULL.
	 */
	properties[PROP_INSPECTOR_LOCK] =
		g_param_spec_string ("inspector-lock", "Inspector Lock", "Locked node GUID", NULL, rw);

	/**
	 * LrgEditorViewState:status:
	 *
	 * The status line text.
	 */
	properties[PROP_STATUS] =
		g_param_spec_string ("status", "Status", "Status line text", NULL, rw);

	/**
	 * LrgEditorViewState:play-state:
	 *
	 * The play-in-editor state (play.state).
	 */
	properties[PROP_PLAY_STATE] =
		g_param_spec_enum ("play-state", "Play State", "Play-in-editor state",
		                   LRG_TYPE_EDITOR_PLAY_STATE, LRG_EDITOR_PLAY_STATE_STOPPED, rw);

	/**
	 * LrgEditorViewState:tile-tool:
	 *
	 * The tile palette tool (tilemap.tool "tool").
	 */
	properties[PROP_TILE_TOOL] =
		g_param_spec_enum ("tile-tool", "Tile Tool", "Tile palette tool",
		                   LRG_TYPE_EDITOR_TILE_TOOL, LRG_EDITOR_TILE_TOOL_BRUSH, rw);

	/**
	 * LrgEditorViewState:tile-mode:
	 *
	 * Whether the viewport's left button paints tiles (tilemap.tool
	 * "mode").
	 */
	properties[PROP_TILE_MODE] =
		g_param_spec_boolean ("tile-mode", "Tile Mode", "Left button paints tiles", FALSE, rw);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * LrgEditorViewState::camera-changed:
	 * @self: the view state
	 * @index: the viewport whose camera changed
	 *
	 * A viewport camera moved, changed projection or was resized.
	 */
	signals[SIGNAL_CAMERA_CHANGED] =
		g_signal_new ("camera-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * LrgEditorViewState::viewport-changed:
	 * @self: the view state
	 * @index: the viewport whose render mode or overlays changed
	 *
	 * A viewport's render mode or overlays changed.
	 */
	signals[SIGNAL_VIEWPORT_CHANGED] =
		g_signal_new ("viewport-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
lrg_editor_view_state_init (LrgEditorViewState *self)
{
	guint i;

	self->layout = LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE;
	self->fov = DEFAULT_FOV;
	self->fly_speed = DEFAULT_FLY_SPEED;
	self->nav_mode = LRG_EDITOR_NAV_MODE_NONE;
	self->play_state = LRG_EDITOR_PLAY_STATE_STOPPED;
	self->tile_tool = LRG_EDITOR_TILE_TOOL_BRUSH;

	/* Four default cameras; the extra viewports of split layouts start
	 * on the classic top / front / right axis views. */
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		Viewport *vp = &self->viewports[i];

		memset (&vp->camera, 0, sizeof vp->camera);
		lrg_editor_view_init_default (&vp->camera.view, 1.0f, 1.0f);
		lrg_editor_view_camera_reset (&vp->camera, (gfloat) self->fov);
		vp->render_mode = LRG_EDITOR_RENDER_MODE_LIT;
		vp->overlays = LRG_EDITOR_VIEW_OVERLAYS_DEFAULT;
	}
	lrg_editor_view_camera_set_axis_view (&self->viewports[1].camera, LRG_EDITOR_AXIS_VIEW_TOP);
	lrg_editor_view_camera_set_axis_view (&self->viewports[2].camera, LRG_EDITOR_AXIS_VIEW_FRONT);
	lrg_editor_view_camera_set_axis_view (&self->viewports[3].camera, LRG_EDITOR_AXIS_VIEW_RIGHT);
}

LrgEditorViewState *
lrg_editor_view_state_new (LrgEditor *editor)
{
	g_return_val_if_fail (editor == NULL || LRG_IS_EDITOR (editor), NULL);

	return g_object_new (LRG_TYPE_EDITOR_VIEW_STATE, "editor", editor, NULL);
}

LrgEditor *
lrg_editor_view_state_get_editor (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), NULL);

	return self->editor;
}

/* --- Layout -------------------------------------------------------------------- */

LrgEditorViewportLayout
lrg_editor_view_state_get_layout (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE);

	return self->layout;
}

void
lrg_editor_view_state_set_layout (LrgEditorViewState      *self,
                                  LrgEditorViewportLayout  layout)
{
	guint old_count;

	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));
	g_return_if_fail (layout <= LRG_EDITOR_VIEWPORT_LAYOUT_QUAD);

	if (self->layout == layout)
		return;

	old_count = layout_count (self->layout);
	g_object_freeze_notify (G_OBJECT (self));
	self->layout = layout;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYOUT]);
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAYOUT_NAME]);
	if (old_count != layout_count (layout))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_VIEWPORT_COUNT]);

	/* The active viewport must exist in the new layout. */
	if (self->active >= layout_count (layout))
		lrg_editor_view_state_set_active_viewport (self, layout_count (layout) - 1);
	g_object_thaw_notify (G_OBJECT (self));
}

const gchar *
lrg_editor_view_state_get_layout_name (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), NULL);

	return enum_nick (LRG_TYPE_EDITOR_VIEWPORT_LAYOUT, (gint) self->layout);
}

gboolean
lrg_editor_view_state_set_layout_name (LrgEditorViewState *self,
                                       const gchar        *name)
{
	gint v = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), FALSE);

	if (!enum_from_nick (LRG_TYPE_EDITOR_VIEWPORT_LAYOUT, name, &v))
		return FALSE;
	lrg_editor_view_state_set_layout (self, (LrgEditorViewportLayout) v);
	return TRUE;
}

guint
lrg_editor_view_state_get_viewport_count (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), 1);

	return layout_count (self->layout);
}

guint
lrg_editor_view_state_get_active_viewport (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), 0);

	return self->active;
}

void
lrg_editor_view_state_set_active_viewport (LrgEditorViewState *self,
                                           guint               index)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	index = MIN (index, layout_count (self->layout) - 1);
	if (self->active == index)
		return;

	g_object_freeze_notify (G_OBJECT (self));
	self->active = index;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_VIEWPORT]);
	notify_active_viewport (self);
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_editor_view_state_next_viewport (LrgEditorViewState *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	lrg_editor_view_state_set_active_viewport (self, (self->active + 1) % layout_count (self->layout));
}

/* --- Cameras ----------------------------------------------------------------------- */

LrgEditorViewCamera *
lrg_editor_view_state_get_camera (LrgEditorViewState *self,
                                  gint                index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), NULL);

	return &self->viewports[resolve_index (self, index)].camera;
}

void
lrg_editor_view_state_set_camera (LrgEditorViewState        *self,
                                  gint                       index,
                                  const LrgEditorViewCamera *camera)
{
	LrgEditorViewCamera *dst;
	LrgEditorViewCamera  next;
	guint                i;

	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));
	g_return_if_fail (camera != NULL);

	i = resolve_index (self, index);
	dst = &self->viewports[i].camera;

	/* The viewport size belongs to the widget, not to the caller. */
	next = *camera;
	next.view.width = dst->view.width;
	next.view.height = dst->view.height;
	if (lrg_editor_view_camera_equal (dst, &next))
		return;
	*dst = next;
	emit_camera_changed (self, i);
}

void
lrg_editor_view_state_camera_changed (LrgEditorViewState *self,
                                      gint                index)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	emit_camera_changed (self, resolve_index (self, index));
}

void
lrg_editor_view_state_get_view (LrgEditorViewState *self,
                                gint                index,
                                LrgEditorView      *out)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));
	g_return_if_fail (out != NULL);

	lrg_editor_view_camera_to_view (&self->viewports[resolve_index (self, index)].camera, out);
}

void
lrg_editor_view_state_set_viewport_size (LrgEditorViewState *self,
                                         gint                index,
                                         gfloat              width,
                                         gfloat              height)
{
	LrgEditorViewCamera *cam;
	guint                i;

	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	i = resolve_index (self, index);
	cam = &self->viewports[i].camera;
	width = MAX (width, 1.0f);
	height = MAX (height, 1.0f);
	if (cam->view.width == width && cam->view.height == height)
		return;
	lrg_editor_view_camera_set_size (cam, width, height);
	emit_camera_changed (self, i);
}

gdouble
lrg_editor_view_state_get_fov (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), DEFAULT_FOV);

	return self->fov;
}

void
lrg_editor_view_state_set_fov (LrgEditorViewState *self,
                               gdouble             fov)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	fov = CLAMP (fov, 10.0, 150.0);
	if (self->fov == fov)
		return;
	self->fov = fov;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FOV]);
}

gdouble
lrg_editor_view_state_get_fly_speed (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), DEFAULT_FLY_SPEED);

	return self->fly_speed;
}

void
lrg_editor_view_state_set_fly_speed (LrgEditorViewState *self,
                                     gdouble             speed)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	speed = CLAMP (speed, MIN_FLY_SPEED, MAX_FLY_SPEED);
	if (self->fly_speed == speed)
		return;
	self->fly_speed = speed;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLY_SPEED]);
}

/* --- View commands ------------------------------------------------------------------ */

/* Runs @change on the active camera and emits camera-changed when the
 * camera actually changed. */
#define ACTIVE_CAMERA_EDIT(self, stmt)                                              \
	G_STMT_START {                                                              \
		LrgEditorViewCamera *cam_ = &(self)->viewports[(self)->active].camera; \
		LrgEditorViewCamera  before_ = *cam_;                                   \
		(void) cam_;                                                            \
		stmt;                                                                   \
		if (!lrg_editor_view_camera_equal (&before_, cam_))                    \
			emit_camera_changed ((self), (self)->active);                   \
	} G_STMT_END

void
lrg_editor_view_state_reset_camera (LrgEditorViewState *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	ACTIVE_CAMERA_EDIT (self, lrg_editor_view_camera_reset (cam_, (gfloat) self->fov));
}

void
lrg_editor_view_state_reset_for_level (LrgEditorViewState *self,
                                       LrgLevel           *level)
{
	gboolean view_2d;
	guint    i;

	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));
	g_return_if_fail (level == NULL || LRG_IS_LEVEL (level));

	/* Loading a level puts the main camera into the level's mode; the
	 * extra viewports go back to their axis views in that mode. */
	view_2d = level != NULL && lrg_level_get_default_2d (level);
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		LrgEditorViewCamera *cam = &self->viewports[i].camera;
		LrgEditorViewCamera  before = *cam;

		lrg_editor_view_camera_set_2d (cam, view_2d, (gfloat) self->fov);
		if (i > 0 && !view_2d)
			lrg_editor_view_camera_set_axis_view (cam, i == 1 ? LRG_EDITOR_AXIS_VIEW_TOP
			                                      : i == 2 ? LRG_EDITOR_AXIS_VIEW_FRONT
			                                               : LRG_EDITOR_AXIS_VIEW_RIGHT);
		if (!lrg_editor_view_camera_equal (&before, cam))
			emit_camera_changed (self, i);
	}
}

void
lrg_editor_view_state_focus_selection (LrgEditorViewState *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));
	g_return_if_fail (self->editor != NULL);

	ACTIVE_CAMERA_EDIT (self, lrg_editor_view_camera_focus_selection (cam_, self->editor, (gfloat) self->fov));
}

void
lrg_editor_view_state_frame_all (LrgEditorViewState *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));
	g_return_if_fail (self->editor != NULL);

	ACTIVE_CAMERA_EDIT (self, lrg_editor_view_camera_frame_all (cam_, lrg_editor_get_level (self->editor),
	                                                            (gfloat) self->fov));
}

void
lrg_editor_view_state_set_axis_view (LrgEditorViewState *self,
                                     LrgEditorAxisView   axis)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	ACTIVE_CAMERA_EDIT (self, lrg_editor_view_camera_set_axis_view (cam_, axis));
}

void
lrg_editor_view_state_orbit_step (LrgEditorViewState *self,
                                  gint                right,
                                  gint                down)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	ACTIVE_CAMERA_EDIT (self, lrg_editor_view_camera_orbit_step (cam_, right, down));
}

void
lrg_editor_view_state_zoom_step (LrgEditorViewState *self,
                                 gboolean            zoom_in)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	ACTIVE_CAMERA_EDIT (self, lrg_editor_view_camera_zoom_step (cam_, zoom_in));
}

void
lrg_editor_view_state_toggle_ortho (LrgEditorViewState *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	ACTIVE_CAMERA_EDIT (self, lrg_editor_view_camera_toggle_ortho (cam_));
}

void
lrg_editor_view_state_toggle_2d (LrgEditorViewState *self)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	ACTIVE_CAMERA_EDIT (self, lrg_editor_view_camera_set_2d (cam_, !cam_->view_2d, (gfloat) self->fov));
}

/* --- Render modes and overlays ------------------------------------------------------- */

LrgEditorRenderMode
lrg_editor_view_state_get_render_mode (LrgEditorViewState *self,
                                       gint                index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), LRG_EDITOR_RENDER_MODE_LIT);

	return self->viewports[resolve_index (self, index)].render_mode;
}

void
lrg_editor_view_state_set_render_mode (LrgEditorViewState  *self,
                                       gint                 index,
                                       LrgEditorRenderMode  mode)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));
	g_return_if_fail (mode <= LRG_EDITOR_RENDER_MODE_LIGHTING_ONLY);

	i = resolve_index (self, index);
	if (self->viewports[i].render_mode == mode)
		return;
	self->viewports[i].render_mode = mode;
	g_signal_emit (self, signals[SIGNAL_VIEWPORT_CHANGED], 0, i);
	if (i == self->active)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RENDER_MODE]);
}

LrgEditorViewOverlays
lrg_editor_view_state_get_overlays (LrgEditorViewState *self,
                                    gint                index)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), LRG_EDITOR_VIEW_OVERLAY_NONE);

	return self->viewports[resolve_index (self, index)].overlays;
}

void
lrg_editor_view_state_set_overlays (LrgEditorViewState    *self,
                                    gint                   index,
                                    LrgEditorViewOverlays  overlays)
{
	guint i;

	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	i = resolve_index (self, index);
	if (self->viewports[i].overlays == overlays)
		return;
	self->viewports[i].overlays = overlays;
	g_signal_emit (self, signals[SIGNAL_VIEWPORT_CHANGED], 0, i);
	if (i == self->active)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OVERLAYS]);
}

gboolean
lrg_editor_view_state_has_overlay (LrgEditorViewState    *self,
                                   gint                   index,
                                   LrgEditorViewOverlays  overlay)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), FALSE);

	return (self->viewports[resolve_index (self, index)].overlays & overlay) == overlay;
}

void
lrg_editor_view_state_toggle_overlay (LrgEditorViewState    *self,
                                      gint                   index,
                                      LrgEditorViewOverlays  overlay)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	lrg_editor_view_state_set_overlays (self, index,
	                                    self->viewports[resolve_index (self, index)].overlays ^ overlay);
}

/* --- UI state --------------------------------------------------------------------------- */

LrgEditorNavMode
lrg_editor_view_state_get_nav_mode (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), LRG_EDITOR_NAV_MODE_NONE);

	return self->nav_mode;
}

void
lrg_editor_view_state_set_nav_mode (LrgEditorViewState *self,
                                    LrgEditorNavMode    mode)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	if (self->nav_mode == mode)
		return;
	self->nav_mode = mode;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAV_MODE]);
}

const gchar *
lrg_editor_view_state_get_dialog (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), NULL);

	return self->dialog;
}

void
lrg_editor_view_state_set_dialog (LrgEditorViewState *self,
                                  const gchar        *dialog)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	set_string (self, &self->dialog, dialog, PROP_DIALOG);
}

gboolean
lrg_editor_view_state_get_palette_open (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), FALSE);

	return self->palette_open;
}

void
lrg_editor_view_state_set_palette_open (LrgEditorViewState *self,
                                        gboolean            open)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	set_bool (self, &self->palette_open, open, PROP_PALETTE_OPEN);
}

const gchar *
lrg_editor_view_state_get_timeline_name (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), NULL);

	return self->timeline_name;
}

void
lrg_editor_view_state_set_timeline_name (LrgEditorViewState *self,
                                         const gchar        *name)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	set_string (self, &self->timeline_name, name, PROP_TIMELINE_NAME);
}

gfloat
lrg_editor_view_state_get_timeline_time (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), 0.0f);

	return self->timeline_time;
}

void
lrg_editor_view_state_set_timeline_time (LrgEditorViewState *self,
                                         gfloat              time)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	time = MAX (time, 0.0f);
	if (self->timeline_time == time)
		return;
	self->timeline_time = time;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMELINE_TIME]);
}

gboolean
lrg_editor_view_state_get_timeline_playing (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), FALSE);

	return self->timeline_playing;
}

void
lrg_editor_view_state_set_timeline_playing (LrgEditorViewState *self,
                                            gboolean            playing)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	set_bool (self, &self->timeline_playing, playing, PROP_TIMELINE_PLAYING);
}

gboolean
lrg_editor_view_state_get_timeline_previewing (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), FALSE);

	return self->timeline_previewing;
}

void
lrg_editor_view_state_set_timeline_previewing (LrgEditorViewState *self,
                                               gboolean            previewing)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	set_bool (self, &self->timeline_previewing, previewing, PROP_TIMELINE_PREVIEWING);
}

const gchar *
lrg_editor_view_state_get_inspector_lock (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), NULL);

	return self->inspector_lock;
}

void
lrg_editor_view_state_set_inspector_lock (LrgEditorViewState *self,
                                          const gchar        *guid)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	set_string (self, &self->inspector_lock, guid, PROP_INSPECTOR_LOCK);
}

const gchar *
lrg_editor_view_state_get_status (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), NULL);

	return self->status;
}

void
lrg_editor_view_state_set_status (LrgEditorViewState *self,
                                  const gchar        *status)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	set_string (self, &self->status, status, PROP_STATUS);
}

LrgEditorPlayState
lrg_editor_view_state_get_play_state (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), LRG_EDITOR_PLAY_STATE_STOPPED);

	return self->play_state;
}

void
lrg_editor_view_state_set_play_state (LrgEditorViewState *self,
                                      LrgEditorPlayState  state)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	if (self->play_state == state)
		return;
	self->play_state = state;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PLAY_STATE]);
}

LrgEditorTileTool
lrg_editor_view_state_get_tile_tool (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), LRG_EDITOR_TILE_TOOL_BRUSH);

	return self->tile_tool;
}

void
lrg_editor_view_state_set_tile_tool (LrgEditorViewState *self,
                                     LrgEditorTileTool   tool)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));
	g_return_if_fail (tool <= LRG_EDITOR_TILE_TOOL_PICK);

	if (self->tile_tool == tool)
		return;
	self->tile_tool = tool;
	g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TILE_TOOL]);
}

gboolean
lrg_editor_view_state_get_tile_mode (LrgEditorViewState *self)
{
	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), FALSE);

	return self->tile_mode;
}

void
lrg_editor_view_state_set_tile_mode (LrgEditorViewState *self,
                                     gboolean            tile_mode)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));

	set_bool (self, &self->tile_mode, tile_mode, PROP_TILE_MODE);
}

/* --- Settings ------------------------------------------------------------------------------ */

/* A string property of the settings, if the settings have one by that
 * name (viewport-layout / viewport-cameras arrive with a later settings
 * change; until then these keys are simply skipped). */
static gboolean
settings_has_string (LrgEditorSettings *settings,
                     const gchar       *name)
{
	GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (settings), name);

	return pspec != NULL && pspec->value_type == G_TYPE_STRING &&
	       (pspec->flags & G_PARAM_READWRITE) == G_PARAM_READWRITE;
}

gchar *
lrg_editor_view_state_cameras_to_string (LrgEditorViewState *self)
{
	GString *s;
	guint    i;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), NULL);

	s = g_string_new (NULL);
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		g_autofree gchar *one = lrg_editor_view_camera_to_string (&self->viewports[i].camera);

		if (i > 0)
			g_string_append_c (s, ';');
		g_string_append (s, one);
	}
	return g_string_free (s, FALSE);
}

guint
lrg_editor_view_state_cameras_from_string (LrgEditorViewState *self,
                                           const gchar        *text)
{
	g_auto(GStrv) parts = NULL;
	guint         i, n = 0;

	g_return_val_if_fail (LRG_IS_EDITOR_VIEW_STATE (self), 0);

	if (text == NULL || *text == '\0')
		return 0;
	parts = g_strsplit (text, ";", LRG_EDITOR_VIEW_MAX_VIEWPORTS);
	for (i = 0; parts[i] != NULL && i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		LrgEditorViewCamera cam = self->viewports[i].camera;

		if (!lrg_editor_view_camera_from_string (&cam, parts[i]))
			continue;
		lrg_editor_view_state_set_camera (self, (gint) i, &cam);
		n++;
	}
	return n;
}

void
lrg_editor_view_state_load_settings (LrgEditorViewState *self,
                                     LrgEditorSettings  *settings)
{
	gdouble  fov = DEFAULT_FOV, fly = DEFAULT_FLY_SPEED;
	gboolean grid = TRUE;
	guint    i;

	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));
	g_return_if_fail (LRG_IS_EDITOR_SETTINGS (settings));

	g_object_get (settings, "camera-fov", &fov, "camera-fly-speed", &fly, "grid-visible", &grid, NULL);
	g_object_freeze_notify (G_OBJECT (self));
	lrg_editor_view_state_set_fov (self, fov);
	lrg_editor_view_state_set_fly_speed (self, fly);

	/* grid-visible is the one overlay the 0.8.0 settings persist; it
	 * applies to every viewport. */
	for (i = 0; i < LRG_EDITOR_VIEW_MAX_VIEWPORTS; i++)
	{
		LrgEditorViewOverlays o = self->viewports[i].overlays;

		o = grid ? (o | LRG_EDITOR_VIEW_OVERLAY_GRID) : (o & ~LRG_EDITOR_VIEW_OVERLAY_GRID);
		lrg_editor_view_state_set_overlays (self, (gint) i, o);
	}

	if (settings_has_string (settings, "viewport-layout"))
	{
		g_autofree gchar *layout = NULL;

		g_object_get (settings, "viewport-layout", &layout, NULL);
		if (layout != NULL && *layout != '\0')
			lrg_editor_view_state_set_layout_name (self, layout);
	}
	if (settings_has_string (settings, "viewport-cameras"))
	{
		g_autofree gchar *cams = NULL;

		g_object_get (settings, "viewport-cameras", &cams, NULL);
		lrg_editor_view_state_cameras_from_string (self, cams);
	}
	g_object_thaw_notify (G_OBJECT (self));
}

void
lrg_editor_view_state_save_settings (LrgEditorViewState *self,
                                     LrgEditorSettings  *settings)
{
	g_return_if_fail (LRG_IS_EDITOR_VIEW_STATE (self));
	g_return_if_fail (LRG_IS_EDITOR_SETTINGS (settings));

	g_object_set (settings, "grid-visible",
	              (self->viewports[0].overlays & LRG_EDITOR_VIEW_OVERLAY_GRID) != 0, NULL);
	if (settings_has_string (settings, "viewport-layout"))
		g_object_set (settings, "viewport-layout", lrg_editor_view_state_get_layout_name (self), NULL);
	if (settings_has_string (settings, "viewport-cameras"))
	{
		g_autofree gchar *cams = lrg_editor_view_state_cameras_to_string (self);

		g_object_set (settings, "viewport-cameras", cams, NULL);
	}
}
