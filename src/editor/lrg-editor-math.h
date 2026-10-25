/* lrg-editor-math.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless 3D math for the editor: node world transforms, the viewport
 * view/projection (#LrgEditorView), screen rays, ray picking, box
 * selection and framing.
 *
 * Conventions (matching raylib / graylib):
 *  - Matrices are 4x4, column-major, stored as 16 floats (m[col * 4 + row]).
 *  - Points are column vectors: world = M * local.
 *  - Euler rotations are radians applied X, then Y, then Z
 *    (R = Rz * Ry * Rx), like raylib's MatrixRotateXYZ.
 *  - Y is up; the camera looks down its local -Z.
 *  - Screen coordinates are pixels with the origin at the top-left of the
 *    viewport.
 *
 * Nothing here needs a window or GL context, so picking and gizmo logic are
 * unit-testable.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Matrix helpers
 * ========================================================================== */

/**
 * lrg_editor_math_identity:
 * @out: (out caller-allocates) (array fixed-size=16): destination matrix
 *
 * Writes the identity matrix.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_math_identity (gfloat *out);

/**
 * lrg_editor_math_compose:
 * @location: (array fixed-size=3): translation
 * @rotation: (array fixed-size=3): Euler XYZ rotation in radians
 * @scale: (array fixed-size=3): scale
 * @out: (out caller-allocates) (array fixed-size=16): destination matrix
 *
 * Builds T * Rz * Ry * Rx * S.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_math_compose (const gfloat *location,
                              const gfloat *rotation,
                              const gfloat *scale,
                              gfloat       *out);

/**
 * lrg_editor_math_decompose:
 * @m: (array fixed-size=16): an affine matrix without shear
 * @location: (out caller-allocates) (array fixed-size=3): translation
 * @rotation: (out caller-allocates) (array fixed-size=3): Euler XYZ radians
 * @scale: (out caller-allocates) (array fixed-size=3): scale (a mirrored
 *   matrix yields a negative X scale)
 *
 * Inverse of lrg_editor_math_compose() for matrices it can produce.
 *
 * Returns: %FALSE if the matrix is degenerate (a zero scale axis)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_math_decompose (const gfloat *m,
                                    gfloat       *location,
                                    gfloat       *rotation,
                                    gfloat       *scale);

/**
 * lrg_editor_math_multiply:
 * @a: (array fixed-size=16): left matrix
 * @b: (array fixed-size=16): right matrix
 * @out: (out caller-allocates) (array fixed-size=16): a * b (may alias)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_math_multiply (const gfloat *a,
                               const gfloat *b,
                               gfloat       *out);

/**
 * lrg_editor_math_invert:
 * @m: (array fixed-size=16): matrix to invert
 * @out: (out caller-allocates) (array fixed-size=16): inverse (may alias)
 *
 * Returns: %FALSE if @m is singular (@out is then the identity)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_math_invert (const gfloat *m,
                                 gfloat       *out);

/**
 * lrg_editor_math_transform_point:
 * @m: (array fixed-size=16): matrix
 * @p: (array fixed-size=3): point
 * @out: (out caller-allocates) (array fixed-size=3): M * p (w = 1, with
 *   perspective divide)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_math_transform_point (const gfloat *m,
                                      const gfloat *p,
                                      gfloat       *out);

/**
 * lrg_editor_math_transform_direction:
 * @m: (array fixed-size=16): matrix
 * @d: (array fixed-size=3): direction
 * @out: (out caller-allocates) (array fixed-size=3): M * d (w = 0)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_math_transform_direction (const gfloat *m,
                                          const gfloat *d,
                                          gfloat       *out);

/* ==========================================================================
 * Node transforms
 * ========================================================================== */

/**
 * lrg_editor_node_get_local_matrix:
 * @node: an #LrgNode
 * @out: (out caller-allocates) (array fixed-size=16): local TRS matrix
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_get_local_matrix (LrgNode *node,
                                       gfloat  *out);

/**
 * lrg_editor_node_get_world_matrix:
 * @node: an #LrgNode
 * @out: (out caller-allocates) (array fixed-size=16): world matrix (the
 *   product of all ancestor local matrices and @node's)
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_get_world_matrix (LrgNode *node,
                                       gfloat  *out);

/**
 * lrg_editor_node_get_world_position:
 * @node: an #LrgNode
 * @out: (out caller-allocates) (array fixed-size=3): world-space origin
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_node_get_world_position (LrgNode *node,
                                         gfloat  *out);

/**
 * lrg_editor_world_to_local:
 * @parent: (nullable): the prospective parent, or %NULL for world space
 * @world: (array fixed-size=16): desired world matrix
 * @location: (out caller-allocates) (array fixed-size=3): local translation
 * @rotation: (out caller-allocates) (array fixed-size=3): local rotation
 * @scale: (out caller-allocates) (array fixed-size=3): local scale
 *
 * Computes the local TRS a child of @parent needs to have world matrix
 * @world.
 *
 * Returns: %FALSE if the parent's matrix is singular
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_world_to_local (LrgNode      *parent,
                                    const gfloat *world,
                                    gfloat       *location,
                                    gfloat       *rotation,
                                    gfloat       *scale);

/**
 * lrg_editor_node_get_local_bounds:
 * @node: an #LrgNode
 * @min: (out caller-allocates) (array fixed-size=3): local AABB minimum
 * @max: (out caller-allocates) (array fixed-size=3): local AABB maximum
 *
 * Returns the node's pickable box in its own space. Primitives use their
 * unit extents (a cube spans -0.5..0.5); 2D shapes are thin; nodes without
 * geometry (groups, lights, cameras, emitters) get a small icon box so they
 * stay clickable.
 *
 * Returns: %TRUE if the node has real geometry, %FALSE for an icon box
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_node_get_local_bounds (LrgNode *node,
                                           gfloat  *min,
                                           gfloat  *max);

/* ==========================================================================
 * Viewport view
 * ========================================================================== */

#define LRG_TYPE_EDITOR_VIEW (lrg_editor_view_get_type ())

/**
 * LrgEditorView:
 * @position: camera position
 * @target: point the camera looks at
 * @up: up vector
 * @fovy: vertical field of view in degrees (perspective)
 * @ortho_height: visible world height (orthographic)
 * @orthographic: %TRUE for an orthographic projection
 * @near_plane: near clip distance
 * @far_plane: far clip distance
 * @width: viewport width in pixels
 * @height: viewport height in pixels
 *
 * A self-contained description of what a viewport shows, used to convert
 * between world space and viewport pixels.
 */
typedef struct
{
	gfloat   position[3];
	gfloat   target[3];
	gfloat   up[3];
	gfloat   fovy;
	gfloat   ortho_height;
	gboolean orthographic;
	gfloat   near_plane;
	gfloat   far_plane;
	gfloat   width;
	gfloat   height;
} LrgEditorView;

LRG_AVAILABLE_IN_ALL
GType lrg_editor_view_get_type (void) G_GNUC_CONST;

/**
 * lrg_editor_view_init_default:
 * @view: (out caller-allocates): the view to initialize
 * @width: viewport width in pixels
 * @height: viewport height in pixels
 *
 * A perspective view at (8, 6, 8) looking at the origin, 45 degree FOV.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_init_default (LrgEditorView *view,
                                   gfloat         width,
                                   gfloat         height);

/**
 * lrg_editor_view_copy:
 * @view: an #LrgEditorView
 *
 * Returns: (transfer full): a heap copy
 */
LRG_AVAILABLE_IN_ALL
LrgEditorView * lrg_editor_view_copy (const LrgEditorView *view);

/**
 * lrg_editor_view_free:
 * @view: an #LrgEditorView from lrg_editor_view_copy()
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_free (LrgEditorView *view);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgEditorView, lrg_editor_view_free)

/**
 * lrg_editor_view_get_view_matrix:
 * @view: an #LrgEditorView
 * @out: (out caller-allocates) (array fixed-size=16): world-to-camera matrix
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_get_view_matrix (const LrgEditorView *view,
                                      gfloat              *out);

/**
 * lrg_editor_view_get_projection_matrix:
 * @view: an #LrgEditorView
 * @out: (out caller-allocates) (array fixed-size=16): camera-to-clip matrix
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_get_projection_matrix (const LrgEditorView *view,
                                            gfloat              *out);

/**
 * lrg_editor_view_project:
 * @view: an #LrgEditorView
 * @world: (array fixed-size=3): world-space point
 * @screen_x: (out) (optional): viewport x in pixels
 * @screen_y: (out) (optional): viewport y in pixels
 * @depth: (out) (optional): distance in front of the camera (view-space -z)
 *
 * Returns: %TRUE if the point is in front of the camera
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_view_project (const LrgEditorView *view,
                                  const gfloat        *world,
                                  gfloat              *screen_x,
                                  gfloat              *screen_y,
                                  gfloat              *depth);

/**
 * lrg_editor_view_get_ray:
 * @view: an #LrgEditorView
 * @screen_x: viewport x in pixels
 * @screen_y: viewport y in pixels
 * @origin: (out caller-allocates) (array fixed-size=3): ray origin
 * @direction: (out caller-allocates) (array fixed-size=3): normalized
 *   direction
 *
 * Builds the world-space ray through a viewport pixel.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_get_ray (const LrgEditorView *view,
                              gfloat               screen_x,
                              gfloat               screen_y,
                              gfloat              *origin,
                              gfloat              *direction);

/**
 * lrg_editor_view_pixel_world_size:
 * @view: an #LrgEditorView
 * @world: (array fixed-size=3): a world-space point
 *
 * Returns: how many world units one viewport pixel spans at @world's
 *   depth (used to keep gizmos a constant on-screen size)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_view_pixel_world_size (const LrgEditorView *view,
                                         const gfloat        *world);

/**
 * lrg_editor_view_orbit:
 * @view: an #LrgEditorView
 * @yaw: rotation around the world Y axis through the target (radians)
 * @pitch: rotation around the camera's right axis (radians); the result is
 *   clamped short of straight up/down
 *
 * Orbits the camera around its target.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_orbit (LrgEditorView *view,
                            gfloat         yaw,
                            gfloat         pitch);

/**
 * lrg_editor_view_pan:
 * @view: an #LrgEditorView
 * @dx: pixels to drag horizontally
 * @dy: pixels to drag vertically
 *
 * Moves camera and target so the point under the cursor follows the drag.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_pan (LrgEditorView *view,
                          gfloat         dx,
                          gfloat         dy);

/**
 * lrg_editor_view_dolly:
 * @view: an #LrgEditorView
 * @factor: distance multiplier (< 1 zooms in)
 *
 * Moves the camera toward/away from the target (scales the orthographic
 * height in orthographic mode).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_dolly (LrgEditorView *view,
                            gfloat         factor);

/**
 * lrg_editor_view_fly:
 * @view: an #LrgEditorView
 * @forward: distance along the view direction
 * @right: distance along the camera right vector
 * @up: distance along world Y
 * @yaw: look yaw delta (radians)
 * @pitch: look pitch delta (radians)
 *
 * Free-fly (right mouse + WASD) navigation: rotates the view direction
 * about the camera position and translates camera and target together.
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_fly (LrgEditorView *view,
                          gfloat         forward,
                          gfloat         right,
                          gfloat         up,
                          gfloat         yaw,
                          gfloat         pitch);

/**
 * lrg_editor_view_frame:
 * @view: an #LrgEditorView
 * @center: (array fixed-size=3): point to frame
 * @radius: radius of the region to fit
 *
 * Keeps the viewing direction and moves the camera so a sphere of @radius
 * at @center fills the view (Focus Selection, "F").
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_frame (LrgEditorView *view,
                            const gfloat  *center,
                            gfloat         radius);

/**
 * lrg_editor_view_align:
 * @view: an #LrgEditorView
 * @direction: (array fixed-size=3): direction from the target to the camera
 *   (e.g. {0, 1, 0} for a top view)
 *
 * Re-aims the camera along @direction at the current distance (numpad
 * views).
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_view_align (LrgEditorView *view,
                            const gfloat  *direction);

/* ==========================================================================
 * Intersections and picking
 * ========================================================================== */

/**
 * lrg_editor_ray_aabb:
 * @origin: (array fixed-size=3): ray origin
 * @direction: (array fixed-size=3): ray direction (need not be normalized)
 * @min: (array fixed-size=3): box minimum
 * @max: (array fixed-size=3): box maximum
 * @t: (out) (optional): ray parameter of the entry point (0 if inside)
 *
 * Returns: %TRUE if the ray hits the box in front of its origin
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_ray_aabb (const gfloat *origin,
                              const gfloat *direction,
                              const gfloat *min,
                              const gfloat *max,
                              gfloat       *t);

/**
 * lrg_editor_ray_plane:
 * @origin: (array fixed-size=3): ray origin
 * @direction: (array fixed-size=3): ray direction
 * @point: (array fixed-size=3): a point on the plane
 * @normal: (array fixed-size=3): the plane normal
 * @t: (out) (optional): ray parameter of the hit
 *
 * Returns: %TRUE if the ray hits the plane in front of its origin
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_ray_plane (const gfloat *origin,
                               const gfloat *direction,
                               const gfloat *point,
                               const gfloat *normal,
                               gfloat       *t);

/**
 * lrg_editor_ray_node:
 * @node: an #LrgNode
 * @origin: (array fixed-size=3): world ray origin
 * @direction: (array fixed-size=3): world ray direction
 * @distance: (out) (optional): world distance to the hit
 *
 * Tests the ray against the node's oriented bounds (its local bounds in its
 * world frame).
 *
 * Returns: %TRUE on a hit
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_ray_node (LrgNode      *node,
                              const gfloat *origin,
                              const gfloat *direction,
                              gfloat       *distance);

/**
 * lrg_editor_pick:
 * @level: an #LrgLevel
 * @origin: (array fixed-size=3): world ray origin
 * @direction: (array fixed-size=3): world ray direction
 * @include_locked: whether locked nodes can be picked
 * @distance: (out) (optional): distance to the hit
 *
 * Finds the nearest visible node hit by the ray. Real geometry wins over
 * icon boxes at equal depth, and a node hidden by an invisible ancestor is
 * not pickable.
 *
 * Returns: (transfer none) (nullable): the picked node
 */
LRG_AVAILABLE_IN_ALL
LrgNode * lrg_editor_pick (LrgLevel     *level,
                           const gfloat *origin,
                           const gfloat *direction,
                           gboolean      include_locked,
                           gfloat       *distance);

/**
 * lrg_editor_pick_all:
 * @level: an #LrgLevel
 * @origin: (array fixed-size=3): world ray origin
 * @direction: (array fixed-size=3): world ray direction
 * @include_locked: whether locked nodes can be picked
 *
 * Returns every node under the ray sorted near to far (for the "pick from
 * overlapping objects" menu and click-cycling).
 *
 * Returns: (transfer container) (element-type LrgNode): hits, nearest first
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_pick_all (LrgLevel     *level,
                                 const gfloat *origin,
                                 const gfloat *direction,
                                 gboolean      include_locked);

/**
 * lrg_editor_box_select:
 * @level: an #LrgLevel
 * @view: the viewport view
 * @x0: one corner x (pixels)
 * @y0: one corner y (pixels)
 * @x1: opposite corner x
 * @y1: opposite corner y
 * @include_locked: whether locked nodes can be selected
 *
 * Returns: (transfer container) (element-type LrgNode): visible nodes whose
 *   world origin projects inside the rectangle, in tree order
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_editor_box_select (LrgLevel            *level,
                                   const LrgEditorView *view,
                                   gfloat               x0,
                                   gfloat               y0,
                                   gfloat               x1,
                                   gfloat               y1,
                                   gboolean             include_locked);

/**
 * lrg_editor_compute_bounds:
 * @nodes: (element-type LrgNode): nodes to enclose (with their subtrees)
 * @center: (out caller-allocates) (array fixed-size=3): bounding sphere
 *   center
 * @radius: (out): bounding sphere radius (at least 0.5)
 *
 * Computes a world-space bounding sphere for framing.
 *
 * Returns: %FALSE if @nodes is empty
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_compute_bounds (GPtrArray *nodes,
                                    gfloat    *center,
                                    gfloat    *radius);

/**
 * lrg_editor_snap_value:
 * @value: a value
 * @increment: snap increment (<= 0 returns @value unchanged)
 *
 * Returns: @value rounded to the nearest multiple of @increment
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_editor_snap_value (gfloat value,
                              gfloat increment);

G_END_DECLS
