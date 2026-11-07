/* lrg-editor-view-state.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEditorViewState: the headless, UI-agnostic view model of the editor.
 *
 * Everything the editor shows that is not part of the document lives
 * here: up to four viewport cameras, the viewport layout and the active
 * viewport, per-viewport render modes and overlays, the navigation mode,
 * the open dialog, the command palette, the timeline playhead, the
 * inspector lock, the status line, the play state and the tile tool.
 * Every value is a notifying property, so widgets, IPC methods and tests
 * observe the same state and the editor works the same --headless.
 *
 * Document state that #LrgEditor already owns (transform tool, snapping,
 * transform space, selection) is NOT duplicated: read it from the
 * #LrgEditorViewState:editor.
 *
 * The camera helpers (lrg_editor_view_camera_*) are pure functions moved
 * from the old immediate-mode app: axis views with Blender-style
 * auto-perspective, orbit steps, keyboard zoom, the orthographic toggle,
 * 2D mode, focus selection and frame all.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-editor-math.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

/**
 * LRG_EDITOR_VIEW_MAX_VIEWPORTS:
 *
 * The largest number of viewports a layout shows (the quad layout).
 */
#define LRG_EDITOR_VIEW_MAX_VIEWPORTS (4)

/**
 * LRG_EDITOR_VIEW_ORBIT_STEP:
 *
 * The numpad orbit step in radians (15 degrees).
 */
#define LRG_EDITOR_VIEW_ORBIT_STEP ((gfloat) G_PI / 12.0f)

/**
 * LrgEditorViewportLayout:
 * @LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE: one viewport
 * @LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_H: two viewports side by side
 * @LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_V: two viewports stacked
 * @LRG_EDITOR_VIEWPORT_LAYOUT_QUAD: four viewports in a 2x2 grid
 *
 * How the viewport panel is divided. The nicks ("single", "split-h",
 * "split-v", "quad") are the strings stored in the settings and used by
 * the view.layout-* actions.
 */
typedef enum
{
	LRG_EDITOR_VIEWPORT_LAYOUT_SINGLE,
	LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_H,
	LRG_EDITOR_VIEWPORT_LAYOUT_SPLIT_V,
	LRG_EDITOR_VIEWPORT_LAYOUT_QUAD
} LrgEditorViewportLayout;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_viewport_layout_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_VIEWPORT_LAYOUT (lrg_editor_viewport_layout_get_type ())

/**
 * LrgEditorRenderMode:
 * @LRG_EDITOR_RENDER_MODE_LIT: shaded with the scene lights
 * @LRG_EDITOR_RENDER_MODE_UNLIT: base colours only
 * @LRG_EDITOR_RENDER_MODE_WIREFRAME: edges only
 * @LRG_EDITOR_RENDER_MODE_NORMALS: world normals as colours
 * @LRG_EDITOR_RENDER_MODE_LIGHTING_ONLY: lighting on a white material
 * @LRG_EDITOR_RENDER_MODE_OVERDRAW: debug view: additive heat map of how
 *   many surfaces cover each pixel (blue = one, red = many)
 * @LRG_EDITOR_RENDER_MODE_UV: debug view: the first texture coordinate set
 *   as red/green with a checker, to spot stretching and missing UVs
 * @LRG_EDITOR_RENDER_MODE_LIGHT_COMPLEXITY: debug view: how many clustered
 *   lights touch each pixel, as a heat map
 * @LRG_EDITOR_RENDER_MODE_AO: debug view: the ambient occlusion term
 *   (SSAO times material occlusion) in greyscale
 * @LRG_EDITOR_RENDER_MODE_REFLECTIONS: debug view: the specular image
 *   based lighting (sky and reflection probes) on a mirror-like surface
 *
 * How a viewport draws the scene. The values match
 * #LrgWorld3DRenderMode one to one (the scene renderer casts between
 * them); the last five are the debug view modes of 0.9.0.
 */
typedef enum
{
	LRG_EDITOR_RENDER_MODE_LIT,
	LRG_EDITOR_RENDER_MODE_UNLIT,
	LRG_EDITOR_RENDER_MODE_WIREFRAME,
	LRG_EDITOR_RENDER_MODE_NORMALS,
	LRG_EDITOR_RENDER_MODE_LIGHTING_ONLY,
	LRG_EDITOR_RENDER_MODE_OVERDRAW,
	LRG_EDITOR_RENDER_MODE_UV,
	LRG_EDITOR_RENDER_MODE_LIGHT_COMPLEXITY,
	LRG_EDITOR_RENDER_MODE_AO,
	LRG_EDITOR_RENDER_MODE_REFLECTIONS
} LrgEditorRenderMode;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_render_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_RENDER_MODE (lrg_editor_render_mode_get_type ())

/**
 * LrgEditorViewOverlays:
 * @LRG_EDITOR_VIEW_OVERLAY_NONE: nothing
 * @LRG_EDITOR_VIEW_OVERLAY_GRID: the ground grid
 * @LRG_EDITOR_VIEW_OVERLAY_GIZMOS: transform gizmo and orientation gizmo
 * @LRG_EDITOR_VIEW_OVERLAY_ICONS: icons for lights, cameras and emitters
 * @LRG_EDITOR_VIEW_OVERLAY_BOUNDS: selection bounding boxes
 * @LRG_EDITOR_VIEW_OVERLAY_STATS: node count, selection and fps readout
 * @LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING: the level's lights (off: a
 *   default sun)
 * @LRG_EDITOR_VIEW_OVERLAY_SHADOWS: cascaded shadows
 * @LRG_EDITOR_VIEW_OVERLAY_SKY: sky dome and weather preview
 * @LRG_EDITOR_VIEW_OVERLAY_POST: the preview post-processing stack
 *
 * What a viewport draws on top of (or into) the scene.
 */
typedef enum /*< flags >*/
{
	LRG_EDITOR_VIEW_OVERLAY_NONE           = 0,
	LRG_EDITOR_VIEW_OVERLAY_GRID           = 1 << 0,
	LRG_EDITOR_VIEW_OVERLAY_GIZMOS         = 1 << 1,
	LRG_EDITOR_VIEW_OVERLAY_ICONS          = 1 << 2,
	LRG_EDITOR_VIEW_OVERLAY_BOUNDS         = 1 << 3,
	LRG_EDITOR_VIEW_OVERLAY_STATS          = 1 << 4,
	LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING = 1 << 5,
	LRG_EDITOR_VIEW_OVERLAY_SHADOWS        = 1 << 6,
	LRG_EDITOR_VIEW_OVERLAY_SKY            = 1 << 7,
	LRG_EDITOR_VIEW_OVERLAY_POST           = 1 << 8
} LrgEditorViewOverlays;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_view_overlays_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_VIEW_OVERLAYS (lrg_editor_view_overlays_get_type ())

/**
 * LRG_EDITOR_VIEW_OVERLAYS_DEFAULT:
 *
 * The overlays a new viewport starts with: grid, gizmos, icons, stats,
 * scene lighting, shadows and sky (bounds and post-processing off).
 */
#define LRG_EDITOR_VIEW_OVERLAYS_DEFAULT \
	(LRG_EDITOR_VIEW_OVERLAY_GRID | LRG_EDITOR_VIEW_OVERLAY_GIZMOS | LRG_EDITOR_VIEW_OVERLAY_ICONS | \
	 LRG_EDITOR_VIEW_OVERLAY_STATS | LRG_EDITOR_VIEW_OVERLAY_SCENE_LIGHTING | \
	 LRG_EDITOR_VIEW_OVERLAY_SHADOWS | LRG_EDITOR_VIEW_OVERLAY_SKY)

/**
 * LrgEditorNavMode:
 * @LRG_EDITOR_NAV_MODE_NONE: no navigation in progress
 * @LRG_EDITOR_NAV_MODE_FLY: right drag: mouse look plus WASD/QE
 * @LRG_EDITOR_NAV_MODE_ORBIT: orbiting around the camera target
 * @LRG_EDITOR_NAV_MODE_PAN: panning the camera and target together
 * @LRG_EDITOR_NAV_MODE_BOX: left drag on empty space: box selection
 * @LRG_EDITOR_NAV_MODE_ZOOM: drag to dolly (blender scheme: Ctrl + middle)
 * @LRG_EDITOR_NAV_MODE_GIZMO: left drag on the orientation gizmo: orbit
 *
 * The viewport navigation in progress. The nicks are the strings the
 * ui.state IPC method reports as "nav" ("none", "fly", "orbit", "pan",
 * "box", "zoom", "gizmo").
 */
typedef enum
{
	LRG_EDITOR_NAV_MODE_NONE,
	LRG_EDITOR_NAV_MODE_FLY,
	LRG_EDITOR_NAV_MODE_ORBIT,
	LRG_EDITOR_NAV_MODE_PAN,
	LRG_EDITOR_NAV_MODE_BOX,
	LRG_EDITOR_NAV_MODE_ZOOM,
	LRG_EDITOR_NAV_MODE_GIZMO
} LrgEditorNavMode;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_nav_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_NAV_MODE (lrg_editor_nav_mode_get_type ())

/**
 * lrg_editor_nav_mode_to_string:
 * @mode: an #LrgEditorNavMode
 *
 * Returns: (transfer none): the ui.state "nav" string of @mode ("none",
 *   "fly", ...), or "?" for an unknown value
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_nav_mode_to_string (LrgEditorNavMode mode);

/**
 * LrgEditorPlayState:
 * @LRG_EDITOR_PLAY_STATE_STOPPED: editing
 * @LRG_EDITOR_PLAY_STATE_PLAYING: play-in-editor is running
 * @LRG_EDITOR_PLAY_STATE_PAUSED: play-in-editor is paused
 *
 * The play-in-editor state.
 */
typedef enum
{
	LRG_EDITOR_PLAY_STATE_STOPPED,
	LRG_EDITOR_PLAY_STATE_PLAYING,
	LRG_EDITOR_PLAY_STATE_PAUSED
} LrgEditorPlayState;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_play_state_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_PLAY_STATE (lrg_editor_play_state_get_type ())

/**
 * LrgEditorAxisView:
 * @LRG_EDITOR_AXIS_VIEW_NONE: a free view (not on an axis)
 * @LRG_EDITOR_AXIS_VIEW_FRONT: looking from +Z (KP_1)
 * @LRG_EDITOR_AXIS_VIEW_BACK: looking from -Z (Ctrl+KP_1)
 * @LRG_EDITOR_AXIS_VIEW_RIGHT: looking from +X (KP_3)
 * @LRG_EDITOR_AXIS_VIEW_LEFT: looking from -X (Ctrl+KP_3)
 * @LRG_EDITOR_AXIS_VIEW_TOP: looking from +Y (KP_7)
 * @LRG_EDITOR_AXIS_VIEW_BOTTOM: looking from -Y (Ctrl+KP_7)
 *
 * The projection preset of a camera: which world axis it looks along.
 */
typedef enum
{
	LRG_EDITOR_AXIS_VIEW_NONE,
	LRG_EDITOR_AXIS_VIEW_FRONT,
	LRG_EDITOR_AXIS_VIEW_BACK,
	LRG_EDITOR_AXIS_VIEW_RIGHT,
	LRG_EDITOR_AXIS_VIEW_LEFT,
	LRG_EDITOR_AXIS_VIEW_TOP,
	LRG_EDITOR_AXIS_VIEW_BOTTOM
} LrgEditorAxisView;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_axis_view_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_AXIS_VIEW (lrg_editor_axis_view_get_type ())

/**
 * LrgEditorTileTool:
 * @LRG_EDITOR_TILE_TOOL_BRUSH: paint tiles
 * @LRG_EDITOR_TILE_TOOL_ERASER: clear tiles
 * @LRG_EDITOR_TILE_TOOL_RECT: fill a dragged rectangle
 * @LRG_EDITOR_TILE_TOOL_FILL: flood fill
 * @LRG_EDITOR_TILE_TOOL_PICK: eyedropper
 *
 * The tile palette tool. The nicks ("brush", "eraser", "rect", "fill",
 * "pick") are the names the tilemap.tool IPC method accepts.
 */
typedef enum
{
	LRG_EDITOR_TILE_TOOL_BRUSH,
	LRG_EDITOR_TILE_TOOL_ERASER,
	LRG_EDITOR_TILE_TOOL_RECT,
	LRG_EDITOR_TILE_TOOL_FILL,
	LRG_EDITOR_TILE_TOOL_PICK
} LrgEditorTileTool;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_tile_tool_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_TILE_TOOL (lrg_editor_tile_tool_get_type ())

/* ==========================================================================
 * LrgEditorViewCamera
 * ========================================================================== */

#define LRG_TYPE_EDITOR_VIEW_CAMERA (lrg_editor_view_camera_get_type ())

/**
 * LrgEditorViewCamera:
 * @view: the projection and pose (position, target, fov, orthographic
 *   height, viewport size); what picking and rendering use
 * @view_2d: the 2D mode camera (looking down -Z at the XY plane)
 * @ortho_from_align: the orthographic projection came from an axis view,
 *   so the next orbit returns to perspective (Blender's auto-perspective)
 * @axis_view: the axis the camera was last aligned to, or
 *   %LRG_EDITOR_AXIS_VIEW_NONE once it orbits or flies away
 *
 * One viewport camera. The pose is stored as the position and target of
 * an #LrgEditorView rather than as angles, so axis views stay exact
 * (a top view looks straight down with screen-up = -Z, as before); the
 * orbit angles and the distance are derived with
 * lrg_editor_view_camera_get_yaw(), lrg_editor_view_camera_get_pitch()
 * and lrg_editor_view_camera_get_distance() and set with
 * lrg_editor_view_camera_set_orbit().
 */
typedef struct
{
	LrgEditorView     view;
	gboolean          view_2d;
	gboolean          ortho_from_align;
	LrgEditorAxisView axis_view;
} LrgEditorViewCamera;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_view_camera_get_type (void) G_GNUC_CONST;

/**
 * lrg_editor_view_camera_new:
 *
 * A perspective camera at (8, 6, 8) looking at the origin, 50 degree
 * field of view, 1x1 pixels.
 *
 * Returns: (transfer full): a new camera
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewCamera * lrg_editor_view_camera_new (void);

/**
 * lrg_editor_view_camera_copy:
 * @camera: an #LrgEditorViewCamera
 *
 * Returns: (transfer full): a heap copy of @camera
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewCamera * lrg_editor_view_camera_copy (const LrgEditorViewCamera *camera);

/**
 * lrg_editor_view_camera_free:
 * @camera: (nullable): a camera from lrg_editor_view_camera_new() or
 *   lrg_editor_view_camera_copy()
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_free (LrgEditorViewCamera *camera);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgEditorViewCamera, lrg_editor_view_camera_free)

/**
 * lrg_editor_view_camera_equal:
 * @a: an #LrgEditorViewCamera
 * @b: an #LrgEditorViewCamera
 *
 * Returns: %TRUE if every field of @a and @b is identical
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_view_camera_equal (const LrgEditorViewCamera *a,
                                       const LrgEditorViewCamera *b);

/**
 * lrg_editor_view_camera_reset:
 * @camera: an #LrgEditorViewCamera
 * @fovy: vertical field of view in degrees for the perspective view
 *
 * Puts the camera back to its home pose, keeping its viewport size and
 * its 2D flag: the default perspective view, or in 2D an orthographic
 * view down -Z from (0, 0, 50) showing 20 world units (the old
 * editor_view_reset()).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_reset (LrgEditorViewCamera *camera,
                                   gfloat               fovy);

/**
 * lrg_editor_view_camera_set_size:
 * @camera: an #LrgEditorViewCamera
 * @width: viewport width in pixels (at least 1)
 * @height: viewport height in pixels (at least 1)
 *
 * Records the viewport size the camera projects into.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_set_size (LrgEditorViewCamera *camera,
                                      gfloat               width,
                                      gfloat               height);

/**
 * lrg_editor_view_camera_to_view:
 * @camera: an #LrgEditorViewCamera
 * @out: (out caller-allocates): the #LrgEditorView for picking and
 *   rendering
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_to_view (const LrgEditorViewCamera *camera,
                                     LrgEditorView             *out);

/**
 * lrg_editor_view_camera_from_view:
 * @camera: an #LrgEditorViewCamera
 * @view: an #LrgEditorView
 *
 * Copies the pose and projection of @view into @camera. The 2D flag is
 * kept; the axis view is cleared unless the new pose still looks along
 * the same axis.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_from_view (LrgEditorViewCamera *camera,
                                       const LrgEditorView *view);

/**
 * lrg_editor_view_camera_get_yaw:
 * @camera: an #LrgEditorViewCamera
 *
 * Returns: the orbit yaw around world Y in radians (0 = camera on +Z)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_view_camera_get_yaw (const LrgEditorViewCamera *camera);

/**
 * lrg_editor_view_camera_get_pitch:
 * @camera: an #LrgEditorViewCamera
 *
 * Returns: the orbit pitch in radians (positive = camera above the
 *   target)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_view_camera_get_pitch (const LrgEditorViewCamera *camera);

/**
 * lrg_editor_view_camera_get_distance:
 * @camera: an #LrgEditorViewCamera
 *
 * Returns: the distance from the camera to its target
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_view_camera_get_distance (const LrgEditorViewCamera *camera);

/**
 * lrg_editor_view_camera_set_orbit:
 * @camera: an #LrgEditorViewCamera
 * @yaw: orbit yaw in radians
 * @pitch: orbit pitch in radians (clamped short of straight up/down)
 * @distance: distance to the target (at least 0.05)
 *
 * Places the camera on the sphere around its target. Clears the axis
 * view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_set_orbit (LrgEditorViewCamera *camera,
                                       gfloat               yaw,
                                       gfloat               pitch,
                                       gfloat               distance);

/**
 * lrg_editor_view_camera_align:
 * @camera: an #LrgEditorViewCamera
 * @x: direction from the target to the camera, X
 * @y: direction from the target to the camera, Y
 * @z: direction from the target to the camera, Z
 *
 * Looks along an axis at the current distance. Outside 2D mode the
 * camera turns orthographic until the next orbit (auto-perspective).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_align (LrgEditorViewCamera *camera,
                                   gfloat               x,
                                   gfloat               y,
                                   gfloat               z);

/**
 * lrg_editor_view_camera_set_axis_view:
 * @camera: an #LrgEditorViewCamera
 * @axis: the axis view (%LRG_EDITOR_AXIS_VIEW_NONE does nothing)
 *
 * The numpad views: KP_1 front, KP_3 right, KP_7 top, with Ctrl the
 * opposite side. Same as lrg_editor_view_camera_align().
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_set_axis_view (LrgEditorViewCamera *camera,
                                           LrgEditorAxisView    axis);

/**
 * lrg_editor_view_camera_orbit:
 * @camera: an #LrgEditorViewCamera
 * @yaw: yaw delta in radians
 * @pitch: pitch delta in radians
 *
 * Orbits around the target. Does nothing in 2D mode; an orthographic
 * view that came from an axis view returns to perspective first.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_orbit (LrgEditorViewCamera *camera,
                                   gfloat               yaw,
                                   gfloat               pitch);

/**
 * lrg_editor_view_camera_orbit_step:
 * @camera: an #LrgEditorViewCamera
 * @right: -1 orbit left (KP_4), +1 orbit right (KP_6), 0 none
 * @down: -1 orbit up (KP_8), +1 orbit down (KP_2), 0 none
 *
 * Numpad orbiting in %LRG_EDITOR_VIEW_ORBIT_STEP increments.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_orbit_step (LrgEditorViewCamera *camera,
                                        gint                 right,
                                        gint                 down);

/**
 * lrg_editor_view_camera_fly:
 * @camera: an #LrgEditorViewCamera
 * @forward: distance along the view direction
 * @right: distance along the camera right vector
 * @up: distance along world Y
 * @yaw: look yaw delta in radians
 * @pitch: look pitch delta in radians
 *
 * Free-fly navigation. Moving or looking (not a pure vertical move)
 * leaves an axis-aligned orthographic view for perspective, as before.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_fly (LrgEditorViewCamera *camera,
                                 gfloat               forward,
                                 gfloat               right,
                                 gfloat               up,
                                 gfloat               yaw,
                                 gfloat               pitch);

/**
 * lrg_editor_view_camera_pan:
 * @camera: an #LrgEditorViewCamera
 * @dx: pixels dragged horizontally
 * @dy: pixels dragged vertically
 *
 * Moves camera and target so the point under the pointer follows it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_pan (LrgEditorViewCamera *camera,
                                 gfloat               dx,
                                 gfloat               dy);

/**
 * lrg_editor_view_camera_dolly:
 * @camera: an #LrgEditorViewCamera
 * @factor: distance multiplier (< 1 zooms in)
 *
 * Moves toward or away from the target (scales the orthographic height
 * in orthographic mode).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_dolly (LrgEditorViewCamera *camera,
                                   gfloat               factor);

/**
 * lrg_editor_view_camera_zoom_step:
 * @camera: an #LrgEditorViewCamera
 * @zoom_in: %TRUE for Zoom In (x0.8), %FALSE for Zoom Out (x1.25)
 *
 * The keyboard zoom of the view.zoom-in / view.zoom-out actions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_zoom_step (LrgEditorViewCamera *camera,
                                       gboolean             zoom_in);

/**
 * lrg_editor_view_camera_toggle_ortho:
 * @camera: an #LrgEditorViewCamera
 *
 * The KP_5 toggle: flips orthographic and perspective explicitly, so a
 * later orbit keeps the projection. Does nothing in 2D mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_toggle_ortho (LrgEditorViewCamera *camera);

/**
 * lrg_editor_view_camera_set_2d:
 * @camera: an #LrgEditorViewCamera
 * @view_2d: whether the camera is a 2D camera
 * @fovy: field of view for the reset perspective view
 *
 * Switches 2D mode and resets the camera (the view.toggle-2d action).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_set_2d (LrgEditorViewCamera *camera,
                                    gboolean             view_2d,
                                    gfloat               fovy);

/**
 * lrg_editor_view_camera_frame:
 * @camera: an #LrgEditorViewCamera
 * @center: (array fixed-size=3): point to frame
 * @radius: radius of the region to fit
 *
 * Keeps the viewing direction and fits a sphere into the view.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_frame (LrgEditorViewCamera *camera,
                                   const gfloat        *center,
                                   gfloat               radius);

/**
 * lrg_editor_view_camera_frame_nodes:
 * @camera: an #LrgEditorViewCamera
 * @nodes: (element-type LrgNode): nodes to fit, with their subtrees
 *
 * Returns: %FALSE (camera unchanged) if @nodes is empty
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_view_camera_frame_nodes (LrgEditorViewCamera *camera,
                                             GPtrArray           *nodes);

/**
 * lrg_editor_view_camera_frame_all:
 * @camera: an #LrgEditorViewCamera
 * @level: the level to fit
 * @fovy: field of view used when the level is empty and the camera resets
 *
 * Frame All (Home): fits every top-level node, or resets the camera when
 * the level is empty.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_frame_all (LrgEditorViewCamera *camera,
                                       LrgLevel            *level,
                                       gfloat               fovy);

/**
 * lrg_editor_view_camera_focus_selection:
 * @camera: an #LrgEditorViewCamera
 * @editor: the editor whose selection is framed
 * @fovy: field of view used when everything is framed and the level is
 *   empty
 *
 * Focus Selection (F): fits the selected root nodes, or frames all when
 * nothing is selected.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_camera_focus_selection (LrgEditorViewCamera *camera,
                                             LrgEditor           *editor,
                                             gfloat               fovy);

/**
 * lrg_editor_view_camera_to_string:
 * @camera: an #LrgEditorViewCamera
 *
 * Serializes the pose and projection as one line of comma separated
 * numbers (position, target, fovy, ortho height, orthographic, 2D, axis
 * view, ortho-from-align), locale independent.
 *
 * Returns: (transfer full): the string
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_view_camera_to_string (const LrgEditorViewCamera *camera);

/**
 * lrg_editor_view_camera_from_string:
 * @camera: an #LrgEditorViewCamera
 * @text: a string from lrg_editor_view_camera_to_string()
 *
 * Restores a camera, keeping its viewport size.
 *
 * Returns: %FALSE (camera unchanged) if @text is malformed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_view_camera_from_string (LrgEditorViewCamera *camera,
                                             const gchar         *text);

/* ==========================================================================
 * LrgEditorViewState
 * ========================================================================== */

#define LRG_TYPE_EDITOR_VIEW_STATE (lrg_editor_view_state_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorViewState, lrg_editor_view_state, LRG, EDITOR_VIEW_STATE, GObject)

/**
 * lrg_editor_view_state_new:
 * @editor: (nullable): the editor whose tool, snapping, selection and
 *   level the view acts on
 *
 * Returns: (transfer full): a new #LrgEditorViewState with four default
 *   cameras and the single layout
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewState * lrg_editor_view_state_new (LrgEditor *editor);

/**
 * lrg_editor_view_state_get_editor:
 * @self: an #LrgEditorViewState
 *
 * The editor owns the transform tool (lrg_editor_get_tool()), snapping
 * and the transform space; the view state never duplicates them.
 *
 * Returns: (transfer none) (nullable): the editor
 */
LRG_AVAILABLE_IN_ALL
LrgEditor * lrg_editor_view_state_get_editor (LrgEditorViewState *self);

/* --- Layout and viewports ------------------------------------------------ */

/**
 * lrg_editor_view_state_get_layout:
 * @self: an #LrgEditorViewState
 *
 * Returns: the viewport layout
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewportLayout lrg_editor_view_state_get_layout (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_layout:
 * @self: an #LrgEditorViewState
 * @layout: the viewport layout
 *
 * Changes the layout; the active viewport is clamped to the new count.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_layout (LrgEditorViewState      *self,
                                       LrgEditorViewportLayout  layout);

/**
 * lrg_editor_view_state_get_layout_name:
 * @self: an #LrgEditorViewState
 *
 * Returns: (transfer none): the layout nick ("single", "split-h",
 *   "split-v", "quad")
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_view_state_get_layout_name (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_layout_name:
 * @self: an #LrgEditorViewState
 * @name: a layout nick
 *
 * Returns: %FALSE (layout unchanged) if @name is not a layout nick
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_view_state_set_layout_name (LrgEditorViewState *self,
                                                const gchar        *name);

/**
 * lrg_editor_view_state_get_viewport_count:
 * @self: an #LrgEditorViewState
 *
 * Returns: how many viewports the layout shows (1, 2 or 4)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_view_state_get_viewport_count (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_get_active_viewport:
 * @self: an #LrgEditorViewState
 *
 * Returns: the index of the viewport keyboard and IPC view commands act on
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_view_state_get_active_viewport (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_active_viewport:
 * @self: an #LrgEditorViewState
 * @index: a viewport index (clamped to the layout's count)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_active_viewport (LrgEditorViewState *self,
                                                guint               index);

/**
 * lrg_editor_view_state_next_viewport:
 * @self: an #LrgEditorViewState
 *
 * Activates the next viewport of the layout, wrapping around
 * (view.next-viewport).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_next_viewport (LrgEditorViewState *self);

/* --- Cameras -------------------------------------------------------------- */

/**
 * lrg_editor_view_state_get_camera:
 * @self: an #LrgEditorViewState
 * @index: a viewport index (0..3), or -1 for the active viewport
 *
 * The camera is owned by the view state. Code that changes it in place
 * calls lrg_editor_view_state_camera_changed() afterwards.
 *
 * Returns: (transfer none): the viewport's camera
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewCamera * lrg_editor_view_state_get_camera (LrgEditorViewState *self,
                                                        gint                index);

/**
 * lrg_editor_view_state_set_camera:
 * @self: an #LrgEditorViewState
 * @index: a viewport index, or -1 for the active viewport
 * @camera: the new camera (copied; its viewport size is ignored)
 *
 * Replaces a camera and emits #LrgEditorViewState::camera-changed when it
 * differs.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_camera (LrgEditorViewState        *self,
                                       gint                       index,
                                       const LrgEditorViewCamera *camera);

/**
 * lrg_editor_view_state_camera_changed:
 * @self: an #LrgEditorViewState
 * @index: a viewport index, or -1 for the active viewport
 *
 * Emits #LrgEditorViewState::camera-changed (and notify::camera for the
 * active viewport) after a camera was changed in place.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_camera_changed (LrgEditorViewState *self,
                                           gint                index);

/**
 * lrg_editor_view_state_get_view:
 * @self: an #LrgEditorViewState
 * @index: a viewport index, or -1 for the active viewport
 * @out: (out caller-allocates): the #LrgEditorView for picking
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_get_view (LrgEditorViewState *self,
                                     gint                index,
                                     LrgEditorView      *out);

/**
 * lrg_editor_view_state_set_viewport_size:
 * @self: an #LrgEditorViewState
 * @index: a viewport index, or -1 for the active viewport
 * @width: width in pixels
 * @height: height in pixels
 *
 * Records the size a viewport is drawn at (emits camera-changed when it
 * changes).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_viewport_size (LrgEditorViewState *self,
                                              gint                index,
                                              gfloat              width,
                                              gfloat              height);

/**
 * lrg_editor_view_state_get_fov:
 * @self: an #LrgEditorViewState
 *
 * Returns: the field of view (degrees) cameras reset to
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_view_state_get_fov (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_fov:
 * @self: an #LrgEditorViewState
 * @fov: field of view in degrees (10..150)
 *
 * Sets the reset field of view (from the camera-fov setting).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_fov (LrgEditorViewState *self,
                                    gdouble             fov);

/**
 * lrg_editor_view_state_get_fly_speed:
 * @self: an #LrgEditorViewState
 *
 * Returns: the fly speed in world units per second
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_view_state_get_fly_speed (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_fly_speed:
 * @self: an #LrgEditorViewState
 * @speed: world units per second (clamped to 0.1..500)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_fly_speed (LrgEditorViewState *self,
                                          gdouble             speed);

/* --- View commands on the active camera ----------------------------------- */

/**
 * lrg_editor_view_state_reset_camera:
 * @self: an #LrgEditorViewState
 *
 * Resets the active camera (Reset Camera).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_reset_camera (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_reset_for_level:
 * @self: an #LrgEditorViewState
 * @level: (nullable): the level just loaded
 *
 * Puts every camera into the level's default mode (lrg_level_get_default_2d())
 * and resets it, as loading a level does.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_reset_for_level (LrgEditorViewState *self,
                                            LrgLevel           *level);

/**
 * lrg_editor_view_state_focus_selection:
 * @self: an #LrgEditorViewState
 *
 * view.focus: frames the editor's selection (or everything) in the
 * active viewport. Needs #LrgEditorViewState:editor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_focus_selection (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_frame_all:
 * @self: an #LrgEditorViewState
 *
 * view.frame-all in the active viewport. Needs #LrgEditorViewState:editor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_frame_all (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_axis_view:
 * @self: an #LrgEditorViewState
 * @axis: the axis view
 *
 * view.top / bottom / front / back / right / left in the active viewport.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_axis_view (LrgEditorViewState *self,
                                          LrgEditorAxisView   axis);

/**
 * lrg_editor_view_state_orbit_step:
 * @self: an #LrgEditorViewState
 * @right: see lrg_editor_view_camera_orbit_step()
 * @down: see lrg_editor_view_camera_orbit_step()
 *
 * view.orbit-* in the active viewport.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_orbit_step (LrgEditorViewState *self,
                                       gint                right,
                                       gint                down);

/**
 * lrg_editor_view_state_zoom_step:
 * @self: an #LrgEditorViewState
 * @zoom_in: %TRUE for view.zoom-in, %FALSE for view.zoom-out
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_zoom_step (LrgEditorViewState *self,
                                      gboolean            zoom_in);

/**
 * lrg_editor_view_state_toggle_ortho:
 * @self: an #LrgEditorViewState
 *
 * view.perspective (KP_5) in the active viewport.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_toggle_ortho (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_toggle_2d:
 * @self: an #LrgEditorViewState
 *
 * view.toggle-2d in the active viewport.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_toggle_2d (LrgEditorViewState *self);

/* --- Render modes and overlays -------------------------------------------- */

/**
 * lrg_editor_view_state_get_render_mode:
 * @self: an #LrgEditorViewState
 * @index: a viewport index, or -1 for the active viewport
 *
 * Returns: the viewport's render mode
 */
LRG_AVAILABLE_IN_ALL
LrgEditorRenderMode lrg_editor_view_state_get_render_mode (LrgEditorViewState *self,
                                                           gint                index);

/**
 * lrg_editor_view_state_set_render_mode:
 * @self: an #LrgEditorViewState
 * @index: a viewport index, or -1 for the active viewport
 * @mode: the render mode
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_render_mode (LrgEditorViewState  *self,
                                            gint                 index,
                                            LrgEditorRenderMode  mode);

/**
 * lrg_editor_view_state_get_overlays:
 * @self: an #LrgEditorViewState
 * @index: a viewport index, or -1 for the active viewport
 *
 * Returns: the viewport's overlays
 */
LRG_AVAILABLE_IN_ALL
LrgEditorViewOverlays lrg_editor_view_state_get_overlays (LrgEditorViewState *self,
                                                          gint                index);

/**
 * lrg_editor_view_state_set_overlays:
 * @self: an #LrgEditorViewState
 * @index: a viewport index, or -1 for the active viewport
 * @overlays: the overlays
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_overlays (LrgEditorViewState    *self,
                                         gint                   index,
                                         LrgEditorViewOverlays  overlays);

/**
 * lrg_editor_view_state_has_overlay:
 * @self: an #LrgEditorViewState
 * @index: a viewport index, or -1 for the active viewport
 * @overlay: one overlay flag
 *
 * Returns: %TRUE if every flag of @overlay is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_view_state_has_overlay (LrgEditorViewState    *self,
                                            gint                   index,
                                            LrgEditorViewOverlays  overlay);

/**
 * lrg_editor_view_state_toggle_overlay:
 * @self: an #LrgEditorViewState
 * @index: a viewport index, or -1 for the active viewport
 * @overlay: overlay flags to flip
 *
 * view.grid, view.icons, view.stats... flip their flag.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_toggle_overlay (LrgEditorViewState    *self,
                                           gint                   index,
                                           LrgEditorViewOverlays  overlay);

/* --- UI state ---------------------------------------------------------------- */

/**
 * lrg_editor_view_state_get_nav_mode:
 * @self: an #LrgEditorViewState
 *
 * Returns: the navigation in progress (written by #LrgEditorViewportNav)
 */
LRG_AVAILABLE_IN_ALL
LrgEditorNavMode lrg_editor_view_state_get_nav_mode (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_nav_mode:
 * @self: an #LrgEditorViewState
 * @mode: the navigation mode
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_nav_mode (LrgEditorViewState *self,
                                         LrgEditorNavMode    mode);

/**
 * lrg_editor_view_state_get_dialog:
 * @self: an #LrgEditorViewState
 *
 * Returns: (transfer none) (nullable): the id of the open modal dialog
 *   ("preferences", "project-settings", "file", "confirm", "new-project",
 *   "new-script", "about", "shortcuts", "recover"), or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_view_state_get_dialog (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_dialog:
 * @self: an #LrgEditorViewState
 * @dialog: (nullable): the open dialog id, or %NULL when none is open
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_dialog (LrgEditorViewState *self,
                                       const gchar        *dialog);

/**
 * lrg_editor_view_state_get_palette_open:
 * @self: an #LrgEditorViewState
 *
 * Returns: whether the command palette (or quick open) is showing
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_view_state_get_palette_open (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_palette_open:
 * @self: an #LrgEditorViewState
 * @open: whether the palette is showing
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_palette_open (LrgEditorViewState *self,
                                             gboolean            open);

/**
 * lrg_editor_view_state_get_timeline_name:
 * @self: an #LrgEditorViewState
 *
 * Returns: (transfer none) (nullable): the timeline the panel shows
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_view_state_get_timeline_name (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_timeline_name:
 * @self: an #LrgEditorViewState
 * @name: (nullable): a timeline name
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_timeline_name (LrgEditorViewState *self,
                                              const gchar        *name);

/**
 * lrg_editor_view_state_get_timeline_time:
 * @self: an #LrgEditorViewState
 *
 * Returns: the timeline playhead in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_view_state_get_timeline_time (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_timeline_time:
 * @self: an #LrgEditorViewState
 * @time: playhead in seconds (negative values become 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_timeline_time (LrgEditorViewState *self,
                                              gfloat              time);

/**
 * lrg_editor_view_state_get_timeline_playing:
 * @self: an #LrgEditorViewState
 *
 * Returns: whether the timeline plays in the editor
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_view_state_get_timeline_playing (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_timeline_playing:
 * @self: an #LrgEditorViewState
 * @playing: whether the timeline plays
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_timeline_playing (LrgEditorViewState *self,
                                                 gboolean            playing);

/**
 * lrg_editor_view_state_get_timeline_previewing:
 * @self: an #LrgEditorViewState
 *
 * Returns: whether nodes show sampled timeline values
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_view_state_get_timeline_previewing (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_timeline_previewing:
 * @self: an #LrgEditorViewState
 * @previewing: whether nodes show sampled values
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_timeline_previewing (LrgEditorViewState *self,
                                                    gboolean            previewing);

/**
 * lrg_editor_view_state_get_inspector_lock:
 * @self: an #LrgEditorViewState
 *
 * Returns: (transfer none) (nullable): GUID of the node the inspector is
 *   locked to, or %NULL when it follows the selection
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_view_state_get_inspector_lock (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_inspector_lock:
 * @self: an #LrgEditorViewState
 * @guid: (nullable): node GUID to lock to, or %NULL to unlock
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_inspector_lock (LrgEditorViewState *self,
                                               const gchar        *guid);

/**
 * lrg_editor_view_state_get_status:
 * @self: an #LrgEditorViewState
 *
 * Returns: (transfer none) (nullable): the status line text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_view_state_get_status (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_status:
 * @self: an #LrgEditorViewState
 * @status: (nullable): status line text
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_status (LrgEditorViewState *self,
                                       const gchar        *status);

/**
 * lrg_editor_view_state_get_play_state:
 * @self: an #LrgEditorViewState
 *
 * Returns: the play-in-editor state
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPlayState lrg_editor_view_state_get_play_state (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_play_state:
 * @self: an #LrgEditorViewState
 * @state: the play state (mirrored from #LrgEditorPlaySession)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_play_state (LrgEditorViewState *self,
                                           LrgEditorPlayState  state);

/**
 * lrg_editor_view_state_get_tile_tool:
 * @self: an #LrgEditorViewState
 *
 * Returns: the tile palette tool
 */
LRG_AVAILABLE_IN_ALL
LrgEditorTileTool lrg_editor_view_state_get_tile_tool (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_tile_tool:
 * @self: an #LrgEditorViewState
 * @tool: the tile tool
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_tile_tool (LrgEditorViewState *self,
                                          LrgEditorTileTool   tool);

/**
 * lrg_editor_view_state_get_tile_mode:
 * @self: an #LrgEditorViewState
 *
 * Returns: whether the viewport's left button paints tiles
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_view_state_get_tile_mode (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_set_tile_mode:
 * @self: an #LrgEditorViewState
 * @tile_mode: whether the left button paints tiles
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_set_tile_mode (LrgEditorViewState *self,
                                          gboolean            tile_mode);

/**
 * lrg_editor_tile_tool_to_string:
 * @tool: an #LrgEditorTileTool
 *
 * Returns: (transfer none): the tool's name ("brush", ...), or "?"
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_tile_tool_to_string (LrgEditorTileTool tool);

/**
 * lrg_editor_tile_tool_from_string:
 * @name: a tool name
 * @tool: (out): the tool
 *
 * Returns: %TRUE if @name is a tile tool name
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_tile_tool_from_string (const gchar       *name,
                                           LrgEditorTileTool *tool);

/* --- Settings ------------------------------------------------------------------ */

/**
 * lrg_editor_view_state_load_settings:
 * @self: an #LrgEditorViewState
 * @settings: the editor settings
 *
 * Applies camera-fov, camera-fly-speed and grid-visible, plus the
 * viewport-layout and viewport-cameras keys when the settings have them.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_load_settings (LrgEditorViewState *self,
                                          LrgEditorSettings  *settings);

/**
 * lrg_editor_view_state_save_settings:
 * @self: an #LrgEditorViewState
 * @settings: the editor settings
 *
 * Writes grid-visible (from the first viewport), plus viewport-layout and
 * viewport-cameras when the settings have those keys. Does not save the
 * settings file.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_state_save_settings (LrgEditorViewState *self,
                                          LrgEditorSettings  *settings);

/**
 * lrg_editor_view_state_cameras_to_string:
 * @self: an #LrgEditorViewState
 *
 * Returns: (transfer full): all four cameras as
 *   lrg_editor_view_camera_to_string() records joined with ";"
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_view_state_cameras_to_string (LrgEditorViewState *self);

/**
 * lrg_editor_view_state_cameras_from_string:
 * @self: an #LrgEditorViewState
 * @text: a string from lrg_editor_view_state_cameras_to_string()
 *
 * Restores the cameras that parse; the rest are left alone.
 *
 * Returns: the number of cameras restored
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_view_state_cameras_from_string (LrgEditorViewState *self,
                                                 const gchar        *text);

G_END_DECLS
