/* lrg-editor-gizmo-paint.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Editor viewport overlays recorded into an #LrgPaintList: the transform
 * gizmo, its drag readout, the orientation axes, the box-select
 * rectangle, the stats chip, the tile hover outline, the play-mode frame,
 * missing-asset boxes and the active-viewport outline.
 *
 * Every function paints in canvas coordinates. Viewport-local pixels
 * (the coordinates #LrgEditorView, #LrgEditorGizmo and
 * #LrgEditorViewportNav use for hit testing) map to the canvas as
 * (@origin_x + u * @scale, @origin_y + v * @scale), so what is drawn and
 * what is hit always come from the same geometry.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../lrg-enums.h"
#include "../../paint/lrg-paint-list.h"
#include "../../ui/lrg-theme.h"
#include "../lrg-editor-math.h"
#include "../lrg-editor-gizmo.h"
#include "../lrg-editor-view-state.h"
#include "../../graphics/lrg-lod-group.h"
#include "../../physics/lrg-dynamics.h"

G_BEGIN_DECLS

/**
 * lrg_editor_gizmo_paint_transform:
 * @list: the paint list
 * @gizmo: the transform gizmo (origin, axes, hovered and active handle)
 * @tool: the editor tool; %LRG_EDITOR_TOOL_SELECT paints nothing
 * @view: the camera the gizmo is seen through, sized to the viewport
 *   image in pixels
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Paints the transform gizmo of @tool: rotation rings plus the screen
 * ring (rotate), arms with arrow heads and the three plane squares
 * (translate) or arms with boxes (scale), and the centre handle. The
 * hovered handle (when not dragging) and the dragged handle are
 * highlighted, the others dim slightly while a drag runs. Nothing is
 * painted when the gizmo is hidden (no selection) or behind the camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_transform (LrgPaintList        *list,
                                       LrgEditorGizmo      *gizmo,
                                       LrgEditorTool        tool,
                                       const LrgEditorView *view,
                                       gfloat               origin_x,
                                       gfloat               origin_y,
                                       gfloat               scale);

/**
 * lrg_editor_gizmo_paint_format_delta:
 * @gizmo: the transform gizmo
 * @tool: the editor tool
 *
 * The drag readout text: "Move  dx 1.25  dy 0  dz -0.5",
 * "Rotate  45.0 deg" or "Scale  x1.50 y1.50 z1.50".
 *
 * Returns: (transfer full) (nullable): the text, or %NULL when no drag
 *   is running
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_editor_gizmo_paint_format_delta (LrgEditorGizmo *gizmo,
                                             LrgEditorTool   tool);

/**
 * lrg_editor_gizmo_paint_drag_readout:
 * @list: the paint list
 * @theme: the theme (font, colours, metrics)
 * @gizmo: the transform gizmo
 * @tool: the editor tool
 * @x: pointer x in canvas coordinates
 * @y: pointer y in canvas coordinates
 *
 * Paints lrg_editor_gizmo_paint_format_delta() on a small rounded chip
 * just below and right of the pointer while a gizmo drag runs.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_drag_readout (LrgPaintList   *list,
                                          LrgTheme       *theme,
                                          LrgEditorGizmo *gizmo,
                                          LrgEditorTool   tool,
                                          gfloat          x,
                                          gfloat          y);

/**
 * lrg_editor_gizmo_paint_axes:
 * @list: the paint list
 * @theme: the theme
 * @camera: the viewport camera; its view size places the gizmo at
 *   (%LRG_EDITOR_NAV_GIZMO_MARGIN, height - %LRG_EDITOR_NAV_GIZMO_MARGIN)
 * @hot: the hovered end (0..5) or %LRG_EDITOR_NAV_AXIS_HOT_DISC, or -1
 * @lit: brighten the disc (hovered or being dragged)
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Paints the orientation gizmo with the geometry of
 * lrg_editor_viewport_nav_get_gizmo_end(): a translucent disc, the
 * +X +Y +Z ends as labelled discs on lines and the negative ends as
 * smaller discs, far ends first. Nothing for a 2D camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_axes (LrgPaintList              *list,
                                  LrgTheme                  *theme,
                                  const LrgEditorViewCamera *camera,
                                  gint                       hot,
                                  gboolean                   lit,
                                  gfloat                     origin_x,
                                  gfloat                     origin_y,
                                  gfloat                     scale);

/**
 * lrg_editor_gizmo_paint_box_select:
 * @list: the paint list
 * @theme: the theme
 * @x: left edge, canvas coordinates
 * @y: top edge
 * @width: width
 * @height: height
 *
 * Paints the box-select rectangle: a translucent accent fill with a
 * crisp accent border.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_box_select (LrgPaintList *list,
                                        LrgTheme     *theme,
                                        gfloat        x,
                                        gfloat        y,
                                        gfloat        width,
                                        gfloat        height);

/**
 * lrg_editor_gizmo_paint_chip:
 * @list: the paint list
 * @theme: the theme
 * @text: the text (one or more lines)
 * @x: anchor x, canvas coordinates
 * @y: top edge
 * @align_right: %TRUE when @x is the chip's right edge
 * @accent: (nullable): text colour, %NULL for the theme's text colour
 *
 * Paints a text chip on a glass background with a soft shadow (the
 * stats overlay, hints).
 *
 * Returns: the chip's height in canvas units
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_gizmo_paint_chip (LrgPaintList   *list,
                                    LrgTheme       *theme,
                                    const gchar    *text,
                                    gfloat          x,
                                    gfloat          y,
                                    gboolean        align_right,
                                    const GrlColor *accent);

/**
 * lrg_editor_gizmo_paint_tile_hover:
 * @list: the paint list
 * @theme: the theme
 * @view: the viewport camera, sized to the viewport image
 * @node: a tilemap node (its "tile_size" visual param sizes the cells)
 * @cell_x: hovered column
 * @cell_y: hovered row
 * @erase: paint the eraser colour instead of the accent
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Outlines the hovered cell of a tilemap (projected through @view) and
 * tints it faintly.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_tile_hover (LrgPaintList        *list,
                                        LrgTheme            *theme,
                                        const LrgEditorView *view,
                                        LrgNode             *node,
                                        gint                 cell_x,
                                        gint                 cell_y,
                                        gboolean             erase,
                                        gfloat               origin_x,
                                        gfloat               origin_y,
                                        gfloat               scale);

/**
 * lrg_editor_gizmo_paint_play_frame:
 * @list: the paint list
 * @theme: the theme
 * @x: viewport left, canvas coordinates
 * @y: viewport top
 * @width: viewport width
 * @height: viewport height
 * @paused: label "PAUSED" instead of "PLAYING"
 *
 * Paints the play-mode frame (a success-coloured, or warning-coloured
 * while paused, border) and a pill label at the bottom centre with the
 * play keys.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_play_frame (LrgPaintList *list,
                                        LrgTheme     *theme,
                                        gfloat        x,
                                        gfloat        y,
                                        gfloat        width,
                                        gfloat        height,
                                        gboolean      paused);

/**
 * lrg_editor_gizmo_paint_missing_asset:
 * @list: the paint list
 * @theme: the theme
 * @view: the viewport camera, sized to the viewport image
 * @node: a node whose asset could not be found
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Paints a magenta box around the screen bounds of @node with a small
 * "missing: file" tag, so broken references stand out in every render
 * mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_missing_asset (LrgPaintList        *list,
                                           LrgTheme            *theme,
                                           const LrgEditorView *view,
                                           LrgNode             *node,
                                           gfloat               origin_x,
                                           gfloat               origin_y,
                                           gfloat               scale);

/**
 * lrg_editor_gizmo_paint_active_outline:
 * @list: the paint list
 * @theme: the theme
 * @x: viewport left, canvas coordinates
 * @y: viewport top
 * @width: viewport width
 * @height: viewport height
 * @strength: 0..1, the outline's opacity (a spring in the panel)
 *
 * Paints the accent outline of the active viewport of a split layout.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_active_outline (LrgPaintList *list,
                                            LrgTheme     *theme,
                                            gfloat        x,
                                            gfloat        y,
                                            gfloat        width,
                                            gfloat        height,
                                            gfloat        strength);

/* ==========================================================================
 * Volume gizmos (0.9.0): light ranges and cones, decal and probe boxes
 * ========================================================================== */

/**
 * LrgEditorVolumeHandle:
 * @LRG_EDITOR_VOLUME_HANDLE_NONE: no handle
 * @LRG_EDITOR_VOLUME_HANDLE_RANGE: the reach of a point or spot light
 *   ("range"): the sphere's right edge, or the centre of a spot cone's base
 * @LRG_EDITOR_VOLUME_HANDLE_CONE: the outer cone of a spot light
 *   ("spot_angle", the full angle in degrees)
 * @LRG_EDITOR_VOLUME_HANDLE_INNER_CONE: the inner (full brightness) cone of
 *   a spot light ("spot_inner_angle", full angle in degrees)
 * @LRG_EDITOR_VOLUME_HANDLE_SIZE_X: a decal box's width ("size_x")
 * @LRG_EDITOR_VOLUME_HANDLE_SIZE_Y: a decal box's projection depth
 *   ("size_y")
 * @LRG_EDITOR_VOLUME_HANDLE_SIZE_Z: a decal box's height ("size_z")
 * @LRG_EDITOR_VOLUME_HANDLE_EXTENT_X: a reflection probe's or probe volume's half size
 *   along X ("extents_x")
 * @LRG_EDITOR_VOLUME_HANDLE_EXTENT_Y: half size along Y ("extents_y")
 * @LRG_EDITOR_VOLUME_HANDLE_EXTENT_Z: half size along Z ("extents_z")
 *
 * The draggable handles of the volume gizmo of the selected node. Each
 * edits one visual param (lrg_editor_volume_handle_get_param()).
 */
typedef enum
{
	LRG_EDITOR_VOLUME_HANDLE_NONE,
	LRG_EDITOR_VOLUME_HANDLE_RANGE,
	LRG_EDITOR_VOLUME_HANDLE_CONE,
	LRG_EDITOR_VOLUME_HANDLE_INNER_CONE,
	LRG_EDITOR_VOLUME_HANDLE_SIZE_X,
	LRG_EDITOR_VOLUME_HANDLE_SIZE_Y,
	LRG_EDITOR_VOLUME_HANDLE_SIZE_Z,
	LRG_EDITOR_VOLUME_HANDLE_EXTENT_X,
	LRG_EDITOR_VOLUME_HANDLE_EXTENT_Y,
	LRG_EDITOR_VOLUME_HANDLE_EXTENT_Z
} LrgEditorVolumeHandle;

/**
 * lrg_editor_volume_handle_get_type:
 *
 * Returns: the #GType of #LrgEditorVolumeHandle
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_volume_handle_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_VOLUME_HANDLE (lrg_editor_volume_handle_get_type ())

/**
 * lrg_editor_volume_handle_get_param:
 * @handle: a handle
 *
 * Returns: (nullable): the visual param @handle edits ("range",
 *   "spot_angle", "size_x", "extents_y", ...), %NULL for
 *   %LRG_EDITOR_VOLUME_HANDLE_NONE
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_volume_handle_get_param (LrgEditorVolumeHandle handle);

/**
 * lrg_editor_volume_has_gizmo:
 * @node: a node
 *
 * Whether @node shows a volume gizmo when selected: point and spot
 * lights (range sphere, cones), decals (projection box), reflection
 * probes (influence box) and probe volumes (light probe box, the
 * extent handles). Directional lights have none.
 *
 * Returns: %TRUE when @node has volume handles
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_volume_has_gizmo (LrgNode *node);

/**
 * lrg_editor_volume_get_handle_position:
 * @view: the camera, sized to the viewport image
 * @node: a node with a volume gizmo
 * @handle: one of its handles
 * @out: (out caller-allocates) (array fixed-size=3): the handle's world
 *   position
 *
 * Returns: %FALSE when @node has no such handle
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_volume_get_handle_position (const LrgEditorView   *view,
                                                LrgNode               *node,
                                                LrgEditorVolumeHandle  handle,
                                                gfloat                *out);

/**
 * lrg_editor_volume_hit_test:
 * @view: the camera, sized to the viewport image
 * @node: the selected node
 * @x: pointer x in viewport pixels
 * @y: pointer y in viewport pixels
 *
 * The volume handle within %LRG_EDITOR_GIZMO_PICK_PIXELS (plus a little)
 * of the pointer, the nearest one when several are.
 *
 * Returns: the handle, or %LRG_EDITOR_VOLUME_HANDLE_NONE
 */
LRG_AVAILABLE_IN_ALL
LrgEditorVolumeHandle lrg_editor_volume_hit_test (const LrgEditorView *view,
                                                  LrgNode             *node,
                                                  gfloat               x,
                                                  gfloat               y);

/**
 * lrg_editor_volume_drag_value:
 * @view: the camera, sized to the viewport image
 * @node: the node being edited
 * @handle: the dragged handle
 * @x: pointer x in viewport pixels
 * @y: pointer y in viewport pixels
 * @out_value: (out): the param value the pointer asks for
 *
 * The value of lrg_editor_volume_handle_get_param() (@handle) that puts
 * the handle under the pointer: the distance along the handle's axis
 * (ranges, half extents, sizes divided by the node's scale) or the cone
 * angle in degrees through the pointer on the cone's base plane. Values
 * are clamped to sensible minimums; the inner cone never exceeds the
 * outer one.
 *
 * Returns: %FALSE when the pointer ray is parallel to the handle's axis
 *   or plane
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_volume_drag_value (const LrgEditorView   *view,
                                       LrgNode               *node,
                                       LrgEditorVolumeHandle  handle,
                                       gfloat                 x,
                                       gfloat                 y,
                                       gdouble               *out_value);

/**
 * lrg_editor_gizmo_paint_volume:
 * @list: the paint list
 * @theme: the theme (chips)
 * @view: the camera, sized to the viewport image
 * @node: the selected node
 * @hot: the hovered or dragged handle (highlighted), or
 *   %LRG_EDITOR_VOLUME_HANDLE_NONE
 * @dragging: whether @hot is being dragged (a value chip follows it)
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Paints @node's volume: a point light's range sphere (silhouette and
 * two great circles), a spot light's outer and inner cones (dashed inner
 * ring), a decal's projection box with its projection direction, or a
 * reflection probe's influence box with the inner blend box; then the
 * handles as dots (yellow when @hot) and, while @dragging, a chip with
 * the edited value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_volume (LrgPaintList          *list,
                                    LrgTheme              *theme,
                                    const LrgEditorView   *view,
                                    LrgNode               *node,
                                    LrgEditorVolumeHandle  hot,
                                    gboolean               dragging,
                                    gfloat                 origin_x,
                                    gfloat                 origin_y,
                                    gfloat                 scale);

/**
 * lrg_editor_gizmo_paint_lod_bounds:
 * @list: the paint list
 * @theme: the theme (chips)
 * @view: the camera, sized to the viewport image
 * @node: a mesh-asset node
 * @group: the node's LOD group (lrg_editor_scene_renderer_get_lod_group())
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Paints the LOD bounding sphere of @node (the sphere the screen size is
 * measured with) as a circle, and a chip with the level drawn now (or
 * the forced one), its triangle count and the current screen size
 * against the level's threshold.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_lod_bounds (LrgPaintList        *list,
                                        LrgTheme            *theme,
                                        const LrgEditorView *view,
                                        LrgNode             *node,
                                        LrgLodGroup         *group,
                                        gfloat               origin_x,
                                        gfloat               origin_y,
                                        gfloat               scale);

/**
 * lrg_editor_lod_node_screen_size:
 * @view: the camera
 * @node: a node
 * @group: its LOD group
 *
 * The screen size (bounding sphere diameter over the view height, the
 * measure LOD thresholds use) of @node's LOD group seen through @view.
 *
 * Returns: the screen size, 0 .. (above 1 when it fills the view)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_lod_node_screen_size (const LrgEditorView *view,
                                        LrgNode             *node,
                                        LrgLodGroup         *group);

/* ==========================================================================
 * Physics gizmos (0.9.0 M5): collider shapes, joints, 2D colliders
 * ========================================================================== */

/**
 * LrgEditorPhysicsHandle:
 * @LRG_EDITOR_PHYSICS_HANDLE_NONE: no handle
 * @LRG_EDITOR_PHYSICS_HANDLE_HALF_X: a box shape's half extent along its
 *   local X ("half-extent-x"), on the +X face
 * @LRG_EDITOR_PHYSICS_HANDLE_HALF_Y: a box's half extent along Y
 *   ("half-extent-y")
 * @LRG_EDITOR_PHYSICS_HANDLE_HALF_Z: a box's half extent along Z
 *   ("half-extent-z")
 * @LRG_EDITOR_PHYSICS_HANDLE_RADIUS: a sphere's, capsule's or cylinder's
 *   radius ("radius"), on the shape's local +X
 * @LRG_EDITOR_PHYSICS_HANDLE_HALF_HEIGHT: a capsule's or cylinder's half
 *   height ("half-height"), on the shape's local +Y (a capsule's handle
 *   sits on the cap's tip)
 * @LRG_EDITOR_PHYSICS_HANDLE_ANCHOR: a joint's anchor ("anchor-x/y/z"),
 *   dragged on the camera-facing plane through it
 * @LRG_EDITOR_PHYSICS_HANDLE_WIDTH_2D: a 2D `collider` component's box
 *   width ("width"), on its right edge
 * @LRG_EDITOR_PHYSICS_HANDLE_HEIGHT_2D: a 2D box's height ("height"), on
 *   its top edge
 * @LRG_EDITOR_PHYSICS_HANDLE_RADIUS_2D: a 2D circle's radius ("radius")
 *
 * The draggable handles of the physics gizmo of the selected node. A
 * handle belongs to one shape or joint of the node's `physics:` block
 * (its index), or to the node's 2D `collider` component description.
 */
typedef enum
{
	LRG_EDITOR_PHYSICS_HANDLE_NONE,
	LRG_EDITOR_PHYSICS_HANDLE_HALF_X,
	LRG_EDITOR_PHYSICS_HANDLE_HALF_Y,
	LRG_EDITOR_PHYSICS_HANDLE_HALF_Z,
	LRG_EDITOR_PHYSICS_HANDLE_RADIUS,
	LRG_EDITOR_PHYSICS_HANDLE_HALF_HEIGHT,
	LRG_EDITOR_PHYSICS_HANDLE_ANCHOR,
	LRG_EDITOR_PHYSICS_HANDLE_WIDTH_2D,
	LRG_EDITOR_PHYSICS_HANDLE_HEIGHT_2D,
	LRG_EDITOR_PHYSICS_HANDLE_RADIUS_2D
} LrgEditorPhysicsHandle;

/**
 * lrg_editor_physics_handle_get_type:
 *
 * Returns: the #GType of #LrgEditorPhysicsHandle
 */
LRG_AVAILABLE_IN_ALL
GType lrg_editor_physics_handle_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_EDITOR_PHYSICS_HANDLE (lrg_editor_physics_handle_get_type ())

/**
 * lrg_editor_physics_handle_get_field:
 * @handle: a handle
 *
 * Returns: (nullable): the field @handle edits: a property name of
 *   #LrgNodePhysicsShape ("half-extent-x", "radius", "half-height"), of
 *   #LrgNodePhysicsJoint ("anchor") or a 2D collider key ("width",
 *   "height", "radius"); %NULL for %LRG_EDITOR_PHYSICS_HANDLE_NONE
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_editor_physics_handle_get_field (LrgEditorPhysicsHandle handle);

/**
 * lrg_editor_physics_has_gizmo:
 * @node: (nullable): a node
 *
 * Whether @node shows a physics gizmo when selected: a `physics:` block
 * with at least one shape or joint, or (2D) a `collider` component
 * description.
 *
 * Returns: %TRUE when @node has a physics gizmo
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_has_gizmo (LrgNode *node);

/**
 * lrg_editor_physics_get_shape_frame:
 * @node: a node with a `physics:` block
 * @index: a shape index
 * @out_matrix: (out caller-allocates) (array fixed-size=16): the shape's
 *   world frame without scale (the node's world position and rotation,
 *   then the shape offset scaled by the node's world scale, then the
 *   shape rotation): what the runtime collider uses
 * @out_scale: (out caller-allocates) (optional) (array fixed-size=3): the
 *   node's absolute world scale, which the runtime bakes into the shape
 *   dimensions
 *
 * Returns: %FALSE when @node has no shape @index
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_get_shape_frame (LrgNode *node,
                                             guint    index,
                                             gfloat  *out_matrix,
                                             gfloat  *out_scale);

/**
 * lrg_editor_physics_get_handle_position:
 * @view: the camera, sized to the viewport image
 * @node: a node with a physics gizmo
 * @handle: one of its handles
 * @index: the shape or joint index the handle belongs to (ignored for the
 *   2D handles)
 * @out: (out caller-allocates) (array fixed-size=3): the handle's world
 *   position
 *
 * Returns: %FALSE when @node has no such handle
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_get_handle_position (const LrgEditorView    *view,
                                                 LrgNode                *node,
                                                 LrgEditorPhysicsHandle  handle,
                                                 guint                   index,
                                                 gfloat                 *out);

/**
 * lrg_editor_physics_hit_test:
 * @view: the camera, sized to the viewport image
 * @node: (nullable): the selected node
 * @x: pointer x in viewport pixels
 * @y: pointer y in viewport pixels
 * @out_index: (out) (optional): the shape or joint index of the handle
 *
 * The physics handle within %LRG_EDITOR_GIZMO_PICK_PIXELS (plus a little)
 * of the pointer, the nearest one when several are.
 *
 * Returns: the handle, or %LRG_EDITOR_PHYSICS_HANDLE_NONE
 */
LRG_AVAILABLE_IN_ALL
LrgEditorPhysicsHandle lrg_editor_physics_hit_test (const LrgEditorView *view,
                                                    LrgNode             *node,
                                                    gfloat               x,
                                                    gfloat               y,
                                                    guint               *out_index);

/**
 * lrg_editor_physics_drag_value:
 * @view: the camera, sized to the viewport image
 * @node: the node being edited
 * @handle: the dragged handle
 * @index: its shape or joint index
 * @x: pointer x in viewport pixels
 * @y: pointer y in viewport pixels
 * @out_value: (out caller-allocates) (array fixed-size=3): the value the
 *   pointer asks for: [0] for a dimension (the distance along the handle's
 *   axis divided by the node's world scale on that axis, clamped to
 *   0.01), all three for an anchor (in the node's local space)
 *
 * The value of lrg_editor_physics_handle_get_field() (@handle) that puts
 * the handle under the pointer.
 *
 * Returns: %FALSE when the pointer ray is parallel to the handle's axis
 *   or plane, or @node has no such handle
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_physics_drag_value (const LrgEditorView    *view,
                                        LrgNode                *node,
                                        LrgEditorPhysicsHandle  handle,
                                        guint                   index,
                                        gfloat                  x,
                                        gfloat                  y,
                                        gfloat                 *out_value);

/**
 * lrg_editor_gizmo_paint_physics_shapes:
 * @list: the paint list
 * @view: the camera, sized to the viewport image
 * @level: (nullable): the level joint targets are looked up in (a line
 *   from the anchor to the target node); %NULL draws no target lines
 * @node: a node
 * @flags: what to paint (#LrgDynamicsDebugFlags): %LRG_DYNAMICS_DEBUG_SHAPES
 *   the shapes, %LRG_DYNAMICS_DEBUG_AABBS their world bounding boxes,
 *   %LRG_DYNAMICS_DEBUG_CONSTRAINTS the joints (anchor, axis, limits, a
 *   line to the target), %LRG_DYNAMICS_DEBUG_CENTER_OF_MASS the body
 *   origin axes
 * @emphasis: 0..1: 1 for the selected node (crisp lines), lower values
 *   fade the lines (other nodes in the authoring overlay)
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Paints the authored `physics:` block of @node (or its 2D `collider`
 * description) as wireframes projected through @view, coloured like the
 * runtime debug draw: green dynamic, blue kinematic, grey static, yellow
 * triggers. Box, sphere, capsule, cylinder and plane shapes have their
 * exact outline; convex and mesh shapes show the node's bounds dashed.
 *
 * Returns: the number of shapes and joints painted
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_gizmo_paint_physics_shapes (LrgPaintList          *list,
                                             const LrgEditorView   *view,
                                             LrgLevel              *level,
                                             LrgNode               *node,
                                             LrgDynamicsDebugFlags  flags,
                                             gfloat                 emphasis,
                                             gfloat                 origin_x,
                                             gfloat                 origin_y,
                                             gfloat                 scale);

/**
 * lrg_editor_gizmo_paint_physics:
 * @list: the paint list
 * @theme: the theme (chips)
 * @view: the camera, sized to the viewport image
 * @level: (nullable): the level joint targets are looked up in
 * @node: the selected node
 * @hot: the hovered or dragged handle, or %LRG_EDITOR_PHYSICS_HANDLE_NONE
 * @hot_index: the shape or joint index of @hot
 * @selected_shape: the shape selected in the Physics panel (drawn
 *   brighter), or -1
 * @dragging: whether @hot is being dragged (a value chip follows it)
 * @origin_x: canvas x of viewport pixel 0
 * @origin_y: canvas y of viewport pixel 0
 * @scale: canvas units per viewport pixel
 *
 * Paints the physics gizmo of the selected node: every shape and joint
 * (lrg_editor_gizmo_paint_physics_shapes()), joint limit arcs and cones,
 * then the handles as dots (yellow when @hot) and, over the hot handle, a
 * chip with its field and value.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_gizmo_paint_physics (LrgPaintList           *list,
                                     LrgTheme               *theme,
                                     const LrgEditorView    *view,
                                     LrgLevel               *level,
                                     LrgNode                *node,
                                     LrgEditorPhysicsHandle  hot,
                                     guint                   hot_index,
                                     gint                    selected_shape,
                                     gboolean                dragging,
                                     gfloat                  origin_x,
                                     gfloat                  origin_y,
                                     gfloat                  scale);

G_END_DECLS
