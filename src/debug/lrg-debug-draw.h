/* lrg-debug-draw.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDebugDraw: an immediate-mode batch of 3D and 2D debug geometry
 * (lines, arrows, boxes, spheres, capsules, cylinders, cones, frustums,
 * grids, axes, circles, points and text labels) with colours,
 * thickness, durations, depth-tested and always-on-top lists and
 * toggleable categories.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../graphics/lrg-camera.h"
#include "../graphics/lrg-camera3d.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

/**
 * LrgDebugShape:
 * @LRG_DEBUG_SHAPE_LINE: a segment
 * @LRG_DEBUG_SHAPE_POLYLINE: connected segments, optionally closed
 * @LRG_DEBUG_SHAPE_ARROW: a segment with a head at its end
 * @LRG_DEBUG_SHAPE_BOX: an axis-aligned box (min/max corners)
 * @LRG_DEBUG_SHAPE_ORIENTED_BOX: a box under a transform
 * @LRG_DEBUG_SHAPE_SPHERE: a sphere (three great circles, or solid)
 * @LRG_DEBUG_SHAPE_CAPSULE: a capsule between two centres
 * @LRG_DEBUG_SHAPE_CYLINDER: a cylinder between two cap centres
 * @LRG_DEBUG_SHAPE_CONE: a cone from an apex to a base centre
 * @LRG_DEBUG_SHAPE_FRUSTUM: the eight corners of an inverse
 *   view-projection
 * @LRG_DEBUG_SHAPE_GRID: a square grid in the XZ plane
 * @LRG_DEBUG_SHAPE_AXES: a transform's X (red), Y (green), Z (blue) axes
 * @LRG_DEBUG_SHAPE_CIRCLE: a circle around a normal
 * @LRG_DEBUG_SHAPE_POINT: a point, drawn as a small three-axis cross
 * @LRG_DEBUG_SHAPE_TEXT: a screen-facing label anchored at a 3D point
 * @LRG_DEBUG_SHAPE_LINE_2D: a 2D segment
 * @LRG_DEBUG_SHAPE_RECT_2D: a 2D rectangle
 * @LRG_DEBUG_SHAPE_CIRCLE_2D: a 2D circle
 * @LRG_DEBUG_SHAPE_TEXT_2D: 2D text
 *
 * The kind of a recorded primitive (lrg_debug_draw_get_shape()).
 */
typedef enum
{
	LRG_DEBUG_SHAPE_LINE,
	LRG_DEBUG_SHAPE_POLYLINE,
	LRG_DEBUG_SHAPE_ARROW,
	LRG_DEBUG_SHAPE_BOX,
	LRG_DEBUG_SHAPE_ORIENTED_BOX,
	LRG_DEBUG_SHAPE_SPHERE,
	LRG_DEBUG_SHAPE_CAPSULE,
	LRG_DEBUG_SHAPE_CYLINDER,
	LRG_DEBUG_SHAPE_CONE,
	LRG_DEBUG_SHAPE_FRUSTUM,
	LRG_DEBUG_SHAPE_GRID,
	LRG_DEBUG_SHAPE_AXES,
	LRG_DEBUG_SHAPE_CIRCLE,
	LRG_DEBUG_SHAPE_POINT,
	LRG_DEBUG_SHAPE_TEXT,
	LRG_DEBUG_SHAPE_LINE_2D,
	LRG_DEBUG_SHAPE_RECT_2D,
	LRG_DEBUG_SHAPE_CIRCLE_2D,
	LRG_DEBUG_SHAPE_TEXT_2D
} LrgDebugShape;

/**
 * lrg_debug_shape_get_type:
 *
 * Returns: the #GType of #LrgDebugShape
 */
LRG_AVAILABLE_IN_ALL
GType lrg_debug_shape_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DEBUG_SHAPE (lrg_debug_shape_get_type ())

/**
 * LrgDebugCategory:
 * @LRG_DEBUG_CATEGORY_GENERAL: anything else (the default category)
 * @LRG_DEBUG_CATEGORY_PHYSICS: collision shapes, contacts, controllers
 * @LRG_DEBUG_CATEGORY_NAVIGATION: nav meshes, paths, agents
 * @LRG_DEBUG_CATEGORY_LIGHTS: light ranges, cones, shadow cascades
 * @LRG_DEBUG_CATEGORY_AUDIO: emitter ranges, listener
 * @LRG_DEBUG_CATEGORY_AI: perception, behaviour state
 * @LRG_DEBUG_CATEGORY_ANIMATION: skeletons, root motion
 * @LRG_DEBUG_CATEGORY_CAMERA: camera frustums and rigs
 * @LRG_DEBUG_CATEGORY_PARTICLES: emitter shapes and bounds
 * @LRG_DEBUG_CATEGORY_EDITOR: editor-only helpers
 * @LRG_DEBUG_CATEGORY_USER: the first category free for games
 *   (16 .. 31 are all theirs)
 *
 * A channel primitives are recorded on.  Categories are indices 0 .. 31
 * of #LrgDebugDraw:category-mask, so the editor can expose one toggle per
 * category; values above %LRG_DEBUG_CATEGORY_USER are valid too.
 */
typedef enum
{
	LRG_DEBUG_CATEGORY_GENERAL = 0,
	LRG_DEBUG_CATEGORY_PHYSICS = 1,
	LRG_DEBUG_CATEGORY_NAVIGATION = 2,
	LRG_DEBUG_CATEGORY_LIGHTS = 3,
	LRG_DEBUG_CATEGORY_AUDIO = 4,
	LRG_DEBUG_CATEGORY_AI = 5,
	LRG_DEBUG_CATEGORY_ANIMATION = 6,
	LRG_DEBUG_CATEGORY_CAMERA = 7,
	LRG_DEBUG_CATEGORY_PARTICLES = 8,
	LRG_DEBUG_CATEGORY_EDITOR = 9,
	LRG_DEBUG_CATEGORY_USER = 16
} LrgDebugCategory;

/**
 * lrg_debug_category_get_type:
 *
 * Returns: the #GType of #LrgDebugCategory
 */
LRG_AVAILABLE_IN_ALL
GType lrg_debug_category_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_DEBUG_CATEGORY (lrg_debug_category_get_type ())

/**
 * LRG_DEBUG_CATEGORY_COUNT:
 *
 * Number of debug categories (0 .. 31).
 */
#define LRG_DEBUG_CATEGORY_COUNT (32)

/**
 * LRG_DEBUG_CATEGORY_MASK_ALL:
 *
 * A category mask enabling every category.
 */
#define LRG_DEBUG_CATEGORY_MASK_ALL (0xFFFFFFFFu)

/**
 * lrg_debug_category_get_name:
 * @category: a category index 0 .. 31
 *
 * Returns: (nullable) (transfer none): the built-in name ("general",
 *   "physics", "navigation", "lights", "audio", "ai", "animation",
 *   "camera", "particles", "editor", "user"), or %NULL for an unnamed
 *   index
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_debug_category_get_name (guint category);

/**
 * lrg_debug_category_from_name:
 * @name: a category name
 *
 * Returns: the category called @name, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_debug_category_from_name (const gchar *name);

/* ==========================================================================
 * Type
 * ========================================================================== */

#define LRG_TYPE_DEBUG_DRAW (lrg_debug_draw_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDebugDraw, lrg_debug_draw, LRG, DEBUG_DRAW, GObject)

/**
 * lrg_debug_draw_new:
 *
 * Creates an empty debug draw batch (a world's own, an editor
 * viewport's own).  Most code records into lrg_debug_draw_get_default().
 *
 * Returns: (transfer full): a new #LrgDebugDraw
 */
LRG_AVAILABLE_IN_ALL
LrgDebugDraw * lrg_debug_draw_new (void);

/**
 * lrg_debug_draw_get_default:
 *
 * The process-wide batch, created on first use.  Gameplay code, physics
 * and navigation record into it unless they were given an instance of
 * their own.
 *
 * Returns: (transfer none): the default #LrgDebugDraw
 */
LRG_AVAILABLE_IN_ALL
LrgDebugDraw * lrg_debug_draw_get_default (void);

/* ==========================================================================
 * Switches
 * ========================================================================== */

/**
 * lrg_debug_draw_set_enabled:
 * @self: an #LrgDebugDraw
 * @enabled: %FALSE to drop every new primitive and draw nothing
 *
 * Sets #LrgDebugDraw:enabled.  Disabling clears what was recorded.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_set_enabled (LrgDebugDraw *self,
                                 gboolean      enabled);

/**
 * lrg_debug_draw_get_enabled:
 * @self: an #LrgDebugDraw
 *
 * Returns: whether the batch records and draws
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_debug_draw_get_enabled (LrgDebugDraw *self);

/**
 * lrg_debug_draw_set_category_mask:
 * @self: an #LrgDebugDraw
 * @mask: bit N enables category N
 *
 * Sets #LrgDebugDraw:category-mask.  Primitives of a disabled category
 * are dropped when recorded and timed ones already recorded are not
 * drawn (they keep ticking, and show again if re-enabled in time).
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_set_category_mask (LrgDebugDraw *self,
                                       guint32       mask);

/**
 * lrg_debug_draw_get_category_mask:
 * @self: an #LrgDebugDraw
 *
 * Returns: the enabled categories (all by default)
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_debug_draw_get_category_mask (LrgDebugDraw *self);

/**
 * lrg_debug_draw_set_category_enabled:
 * @self: an #LrgDebugDraw
 * @category: a category 0 .. 31 (#LrgDebugCategory or a user index)
 * @enabled: whether the category records and draws
 *
 * Toggles one bit of #LrgDebugDraw:category-mask.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_set_category_enabled (LrgDebugDraw *self,
                                          guint         category,
                                          gboolean      enabled);

/**
 * lrg_debug_draw_is_category_enabled:
 * @self: an #LrgDebugDraw
 * @category: a category 0 .. 31
 *
 * Whether a primitive recorded on @category now would be kept.  Code
 * whose debug geometry is costly to compute (a nav mesh walk) tests
 * this first.
 *
 * Returns: %TRUE when the batch is enabled and the category on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_debug_draw_is_category_enabled (LrgDebugDraw *self,
                                             guint         category);

/* ==========================================================================
 * Pen (state applied to the primitives recorded next)
 * ========================================================================== */

/**
 * lrg_debug_draw_set_category:
 * @self: an #LrgDebugDraw
 * @category: the category of the primitives recorded next (0 .. 31)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_set_category (LrgDebugDraw *self,
                                  guint         category);

/**
 * lrg_debug_draw_get_category:
 * @self: an #LrgDebugDraw
 *
 * Returns: the pen's category (%LRG_DEBUG_CATEGORY_GENERAL by default)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_debug_draw_get_category (LrgDebugDraw *self);

/**
 * lrg_debug_draw_set_depth_test:
 * @self: an #LrgDebugDraw
 * @depth_test: %TRUE (default) for the depth-tested list, %FALSE for
 *   the always-on-top list
 *
 * Which list the primitives recorded next go to.  Depth-tested
 * primitives are hidden behind scene geometry; always-on-top ones are
 * drawn after them with the depth test off.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_set_depth_test (LrgDebugDraw *self,
                                    gboolean      depth_test);

/**
 * lrg_debug_draw_get_depth_test:
 * @self: an #LrgDebugDraw
 *
 * Returns: whether the pen records into the depth-tested list
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_debug_draw_get_depth_test (LrgDebugDraw *self);

/**
 * lrg_debug_draw_set_thickness:
 * @self: an #LrgDebugDraw
 * @thickness: line width in pixels (1 by default; clamped to 0.5 .. 64)
 *
 * Lines up to 1 pixel are GL lines.  Wider ones are drawn as
 * camera-facing quads sized so they cover @thickness pixels at their
 * distance, because a GL 3.3 core context only guarantees a line width
 * of 1 (glLineWidth() above 1 is an error in forward-compatible
 * contexts).
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_set_thickness (LrgDebugDraw *self,
                                   gfloat        thickness);

/**
 * lrg_debug_draw_get_thickness:
 * @self: an #LrgDebugDraw
 *
 * Returns: the pen's line width in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_debug_draw_get_thickness (LrgDebugDraw *self);

/**
 * lrg_debug_draw_set_solid:
 * @self: an #LrgDebugDraw
 * @solid: %TRUE to fill boxes, spheres, capsules, cylinders, cones,
 *   circles and 2D rectangles/circles with triangles instead of
 *   outlining them
 *
 * Solid shapes take the colour's alpha (blended, no depth writes when
 * below 255).  Lines, arrows, grids, axes, frustums and text ignore it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_set_solid (LrgDebugDraw *self,
                               gboolean      solid);

/**
 * lrg_debug_draw_get_solid:
 * @self: an #LrgDebugDraw
 *
 * Returns: whether the pen fills shapes
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_debug_draw_get_solid (LrgDebugDraw *self);

/**
 * lrg_debug_draw_set_segments:
 * @self: an #LrgDebugDraw
 * @segments: segments per full circle of round shapes (4 .. 128;
 *   16 by default)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_set_segments (LrgDebugDraw *self,
                                  guint         segments);

/**
 * lrg_debug_draw_get_segments:
 * @self: an #LrgDebugDraw
 *
 * Returns: segments per full circle of round shapes
 */
LRG_AVAILABLE_IN_ALL
guint lrg_debug_draw_get_segments (LrgDebugDraw *self);

/**
 * lrg_debug_draw_set_text_size:
 * @self: an #LrgDebugDraw
 * @size: font size in pixels of text recorded next (4 .. 256; 16 by
 *   default)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_set_text_size (LrgDebugDraw *self,
                                   guint         size);

/**
 * lrg_debug_draw_get_text_size:
 * @self: an #LrgDebugDraw
 *
 * Returns: the pen's font size in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_debug_draw_get_text_size (LrgDebugDraw *self);

/**
 * lrg_debug_draw_push:
 * @self: an #LrgDebugDraw
 *
 * Saves the pen (category, depth test, thickness, solid, segments,
 * text size) so a subsystem can change it and lrg_debug_draw_pop() it
 * back.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_push (LrgDebugDraw *self);

/**
 * lrg_debug_draw_pop:
 * @self: an #LrgDebugDraw
 *
 * Restores the pen saved by the matching lrg_debug_draw_push() (warns
 * and does nothing when none is saved).
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_pop (LrgDebugDraw *self);

/* ==========================================================================
 * 3D primitives
 *
 * Every call takes a colour (%NULL: white) and a duration in seconds:
 * 0 or less keeps the primitive for one frame (until the next
 * lrg_debug_draw_end_frame() or lrg_debug_draw_flush()), a positive one
 * keeps it until lrg_debug_draw_tick() has consumed that much time.
 * ========================================================================== */

/**
 * lrg_debug_draw_line:
 * @self: an #LrgDebugDraw
 * @from: start point
 * @to: end point
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_line (LrgDebugDraw     *self,
                          const GrlVector3 *from,
                          const GrlVector3 *to,
                          const GrlColor   *color,
                          gfloat            duration);

/**
 * lrg_debug_draw_polyline:
 * @self: an #LrgDebugDraw
 * @points: (array length=n_points): the points (copied)
 * @n_points: number of points (at least 2)
 * @closed: also join the last point to the first
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_polyline (LrgDebugDraw     *self,
                              const GrlVector3 *points,
                              guint             n_points,
                              gboolean          closed,
                              const GrlColor   *color,
                              gfloat            duration);

/**
 * lrg_debug_draw_arrow:
 * @self: an #LrgDebugDraw
 * @from: tail
 * @to: tip
 * @head_size: length of the head's barbs (world units; <= 0: a fifth
 *   of the arrow's length)
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 *
 * A segment with four barbs at @to.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_arrow (LrgDebugDraw     *self,
                           const GrlVector3 *from,
                           const GrlVector3 *to,
                           gfloat            head_size,
                           const GrlColor   *color,
                           gfloat            duration);

/**
 * lrg_debug_draw_box:
 * @self: an #LrgDebugDraw
 * @min: the box's lowest corner
 * @max: the box's highest corner
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 *
 * An axis-aligned box: 12 edges, or 12 triangles when solid.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_box (LrgDebugDraw     *self,
                         const GrlVector3 *min,
                         const GrlVector3 *max,
                         const GrlColor   *color,
                         gfloat            duration);

/**
 * lrg_debug_draw_oriented_box:
 * @self: an #LrgDebugDraw
 * @transform: (array fixed-size=16): column-major matrix placing the
 *   box (its origin is the box's centre)
 * @half_extents: half the box's size along each local axis
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_oriented_box (LrgDebugDraw     *self,
                                  const gfloat     *transform,
                                  const GrlVector3 *half_extents,
                                  const GrlColor   *color,
                                  gfloat            duration);

/**
 * lrg_debug_draw_sphere:
 * @self: an #LrgDebugDraw
 * @center: centre
 * @radius: radius
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 *
 * Wire: three great circles (XY, XZ, YZ).  Solid: a latitude/longitude
 * mesh.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_sphere (LrgDebugDraw     *self,
                            const GrlVector3 *center,
                            gfloat            radius,
                            const GrlColor   *color,
                            gfloat            duration);

/**
 * lrg_debug_draw_capsule:
 * @self: an #LrgDebugDraw
 * @a: centre of one hemisphere
 * @b: centre of the other hemisphere
 * @radius: radius
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_capsule (LrgDebugDraw     *self,
                             const GrlVector3 *a,
                             const GrlVector3 *b,
                             gfloat            radius,
                             const GrlColor   *color,
                             gfloat            duration);

/**
 * lrg_debug_draw_cylinder:
 * @self: an #LrgDebugDraw
 * @a: centre of one cap
 * @b: centre of the other cap
 * @radius: radius
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_cylinder (LrgDebugDraw     *self,
                              const GrlVector3 *a,
                              const GrlVector3 *b,
                              gfloat            radius,
                              const GrlColor   *color,
                              gfloat            duration);

/**
 * lrg_debug_draw_cone:
 * @self: an #LrgDebugDraw
 * @apex: the tip
 * @base: centre of the base circle
 * @radius: base radius
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 *
 * A spot light's cone, a perception cone.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_cone (LrgDebugDraw     *self,
                          const GrlVector3 *apex,
                          const GrlVector3 *base,
                          gfloat            radius,
                          const GrlColor   *color,
                          gfloat            duration);

/**
 * lrg_debug_draw_frustum:
 * @self: an #LrgDebugDraw
 * @view_projection: (array fixed-size=16): column-major projection x
 *   view matrix (OpenGL clip space)
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 *
 * The 12 edges of the volume @view_projection maps to the clip cube
 * (its inverse applied to the cube's corners).  A singular matrix is
 * ignored.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_frustum (LrgDebugDraw   *self,
                             const gfloat   *view_projection,
                             const GrlColor *color,
                             gfloat          duration);

/**
 * lrg_debug_draw_camera_frustum:
 * @self: an #LrgDebugDraw
 * @camera: the camera to show
 * @aspect: width over height (<= 0: the camera's #LrgCamera3D:aspect
 *   when set, else 16:9)
 * @near_plane: near distance (<= 0: rlgl's default)
 * @far_plane: far distance (<= 0: rlgl's default)
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 *
 * The frustum of @camera's current pose and projection (perspective or
 * orthographic), as lrg_debug_draw_frustum().
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_camera_frustum (LrgDebugDraw   *self,
                                    LrgCamera3D    *camera,
                                    gfloat          aspect,
                                    gfloat          near_plane,
                                    gfloat          far_plane,
                                    const GrlColor *color,
                                    gfloat          duration);

/**
 * lrg_debug_draw_grid:
 * @self: an #LrgDebugDraw
 * @center: the grid's centre (it lies in the XZ plane through it)
 * @spacing: distance between lines
 * @cells: cells per side (at least 1)
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_grid (LrgDebugDraw     *self,
                          const GrlVector3 *center,
                          gfloat            spacing,
                          guint             cells,
                          const GrlColor   *color,
                          gfloat            duration);

/**
 * lrg_debug_draw_axes:
 * @self: an #LrgDebugDraw
 * @transform: (array fixed-size=16): column-major matrix whose axes
 *   are shown
 * @size: length of each axis line (world units)
 * @duration: seconds to keep it (0: one frame)
 *
 * The transform's X, Y and Z axes in red, green and blue from its
 * origin (a gizmo-like marker for any entity).
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_axes (LrgDebugDraw *self,
                          const gfloat *transform,
                          gfloat        size,
                          gfloat        duration);

/**
 * lrg_debug_draw_circle:
 * @self: an #LrgDebugDraw
 * @center: centre
 * @normal: the circle's normal (need not be unit; zero: +Y)
 * @radius: radius
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_circle (LrgDebugDraw     *self,
                            const GrlVector3 *center,
                            const GrlVector3 *normal,
                            gfloat            radius,
                            const GrlColor   *color,
                            gfloat            duration);

/**
 * lrg_debug_draw_point:
 * @self: an #LrgDebugDraw
 * @position: the point
 * @size: size of the cross marking it (world units)
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 *
 * A point drawn as a three-axis cross (GL points are not sized
 * reliably on GL 3.3 core).
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_point (LrgDebugDraw     *self,
                           const GrlVector3 *position,
                           gfloat            size,
                           const GrlColor   *color,
                           gfloat            duration);

/**
 * lrg_debug_draw_text:
 * @self: an #LrgDebugDraw
 * @position: the 3D anchor
 * @text: the label (copied)
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 *
 * A screen-facing label centred over @position at the pen's text size.
 * It is projected with the matrices of the last 3D draw and drawn by
 * lrg_debug_draw_draw_labels() (lrg_debug_draw_flush() does both);
 * anchors behind the camera are skipped.  Labels are always on top.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_text (LrgDebugDraw     *self,
                          const GrlVector3 *position,
                          const gchar      *text,
                          const GrlColor   *color,
                          gfloat            duration);

/* ==========================================================================
 * 2D primitives (screen space, or a 2D camera's space)
 * ========================================================================== */

/**
 * lrg_debug_draw_line_2d:
 * @self: an #LrgDebugDraw
 * @from: start point
 * @to: end point
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_line_2d (LrgDebugDraw     *self,
                             const GrlVector2 *from,
                             const GrlVector2 *to,
                             const GrlColor   *color,
                             gfloat            duration);

/**
 * lrg_debug_draw_rect_2d:
 * @self: an #LrgDebugDraw
 * @x: left edge
 * @y: top edge
 * @width: width
 * @height: height
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 *
 * Outline, or filled when the pen is solid.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_rect_2d (LrgDebugDraw   *self,
                             gfloat          x,
                             gfloat          y,
                             gfloat          width,
                             gfloat          height,
                             const GrlColor *color,
                             gfloat          duration);

/**
 * lrg_debug_draw_circle_2d:
 * @self: an #LrgDebugDraw
 * @center: centre
 * @radius: radius
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_circle_2d (LrgDebugDraw     *self,
                               const GrlVector2 *center,
                               gfloat            radius,
                               const GrlColor   *color,
                               gfloat            duration);

/**
 * lrg_debug_draw_text_2d:
 * @self: an #LrgDebugDraw
 * @position: top-left corner
 * @text: the text (copied)
 * @color: (nullable): colour (%NULL: white)
 * @duration: seconds to keep it (0: one frame)
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_text_2d (LrgDebugDraw     *self,
                             const GrlVector2 *position,
                             const gchar      *text,
                             const GrlColor   *color,
                             gfloat            duration);

/* ==========================================================================
 * Lifetime
 * ========================================================================== */

/**
 * lrg_debug_draw_tick:
 * @self: an #LrgDebugDraw
 * @delta: seconds elapsed (negative values count as 0)
 *
 * Ages timed primitives and removes those whose duration ran out.
 * One-frame primitives are untouched (lrg_debug_draw_end_frame()
 * removes them).  Call once per frame with the frame's delta time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_tick (LrgDebugDraw *self,
                          gfloat        delta);

/**
 * lrg_debug_draw_end_frame:
 * @self: an #LrgDebugDraw
 *
 * Removes every one-frame primitive.  lrg_debug_draw_flush() calls it
 * after drawing; call it yourself when one batch is drawn into several
 * viewports with lrg_debug_draw_draw_3d().
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_end_frame (LrgDebugDraw *self);

/**
 * lrg_debug_draw_clear:
 * @self: an #LrgDebugDraw
 *
 * Removes every primitive, timed or not.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_clear (LrgDebugDraw *self);

/**
 * lrg_debug_draw_clear_category:
 * @self: an #LrgDebugDraw
 * @category: a category 0 .. 31
 *
 * Removes every primitive of @category (a subsystem redrawing its
 * timed markers from scratch).
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_clear_category (LrgDebugDraw *self,
                                    guint         category);

/* ==========================================================================
 * Drawing
 * ========================================================================== */

/**
 * lrg_debug_draw_draw_3d:
 * @self: an #LrgDebugDraw
 *
 * Draws the 3D primitives inside the caller's open 3D mode (any
 * BeginMode3D(), lrg_camera_begin() or editor viewport): the
 * depth-tested list first, then the always-on-top list with the depth
 * test off.  Thin lines go out as one GL_LINES run and every triangle
 * (solid shapes, thick lines) as one GL_TRIANGLES run per list, so a
 * frame costs a handful of draw calls.  Also captures the matrices
 * lrg_debug_draw_draw_labels() projects labels with and wide lines are
 * sized by, so call it at the top level of the 3D mode, not inside an
 * rlPushMatrix() block (rlgl moves a pushed modelview into a CPU-side
 * transform the batch cannot read).  Without a graphics context it does
 * nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_draw_3d (LrgDebugDraw *self);

/**
 * lrg_debug_draw_draw_labels:
 * @self: an #LrgDebugDraw
 *
 * Draws the 3D text labels in screen space, projected with the matrices
 * the last lrg_debug_draw_draw_3d() saw.  Call after the 3D mode ends.
 * Without a graphics context it does nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_draw_labels (LrgDebugDraw *self);

/**
 * lrg_debug_draw_draw_2d:
 * @self: an #LrgDebugDraw
 *
 * Draws the 2D primitives in the current 2D space (the screen, or
 * inside BeginMode2D()/lrg_camera_begin() of a 2D camera).  Without a
 * graphics context it does nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_draw_2d (LrgDebugDraw *self);

/**
 * lrg_debug_draw_flush:
 * @self: an #LrgDebugDraw
 * @camera: (nullable): the camera to draw with
 *
 * Draws everything and ends the frame:
 *
 * - an #LrgCamera3D: lrg_camera_begin(), lrg_debug_draw_draw_3d(),
 *   lrg_camera_end(), lrg_debug_draw_draw_labels(), then the 2D
 *   primitives in screen space;
 * - an #LrgCamera2D (any other camera): the 2D primitives inside the
 *   camera, then nothing 3D;
 * - %NULL: the 2D primitives in screen space only.
 *
 * Then lrg_debug_draw_end_frame().  Headless, only the frame ends.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_flush (LrgDebugDraw *self,
                           LrgCamera    *camera);

/**
 * lrg_debug_draw_flush_matrices:
 * @self: an #LrgDebugDraw
 * @view: (array fixed-size=16): column-major view matrix
 * @projection: (array fixed-size=16): column-major projection matrix
 *
 * As lrg_debug_draw_flush() with a 3D camera, for render code that has
 * matrices instead of an #LrgCamera3D (a shadow cascade, an editor
 * viewport): loads them into rlgl, draws the 3D lists, restores the
 * previous matrices, draws the labels and the 2D primitives, then ends
 * the frame.  Call outside any 3D mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_debug_draw_flush_matrices (LrgDebugDraw *self,
                                    const gfloat *view,
                                    const gfloat *projection);

/* ==========================================================================
 * Inspection (headless tests, tools)
 * ========================================================================== */

/**
 * lrg_debug_draw_get_count:
 * @self: an #LrgDebugDraw
 *
 * Returns: the number of recorded primitives, in recording order
 *   (index them with the getters below)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_debug_draw_get_count (LrgDebugDraw *self);

/**
 * lrg_debug_draw_get_shape:
 * @self: an #LrgDebugDraw
 * @index: a primitive index
 *
 * Returns: the primitive's shape (%LRG_DEBUG_SHAPE_LINE out of range)
 */
LRG_AVAILABLE_IN_ALL
LrgDebugShape lrg_debug_draw_get_shape (LrgDebugDraw *self,
                                        guint         index);

/**
 * lrg_debug_draw_get_primitive_category:
 * @self: an #LrgDebugDraw
 * @index: a primitive index
 *
 * Returns: the primitive's category (0 out of range)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_debug_draw_get_primitive_category (LrgDebugDraw *self,
                                             guint         index);

/**
 * lrg_debug_draw_get_primitive_depth_test:
 * @self: an #LrgDebugDraw
 * @index: a primitive index
 *
 * Returns: %TRUE for the depth-tested list, %FALSE for always-on-top
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_debug_draw_get_primitive_depth_test (LrgDebugDraw *self,
                                                  guint         index);

/**
 * lrg_debug_draw_get_primitive_solid:
 * @self: an #LrgDebugDraw
 * @index: a primitive index
 *
 * Returns: whether the primitive is filled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_debug_draw_get_primitive_solid (LrgDebugDraw *self,
                                             guint         index);

/**
 * lrg_debug_draw_get_primitive_thickness:
 * @self: an #LrgDebugDraw
 * @index: a primitive index
 *
 * Returns: the primitive's line width in pixels (0 out of range)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_debug_draw_get_primitive_thickness (LrgDebugDraw *self,
                                               guint         index);

/**
 * lrg_debug_draw_get_primitive_remaining:
 * @self: an #LrgDebugDraw
 * @index: a primitive index
 *
 * Returns: seconds left for a timed primitive, 0 for a one-frame one
 *   (or out of range)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_debug_draw_get_primitive_remaining (LrgDebugDraw *self,
                                               guint         index);

/**
 * lrg_debug_draw_get_primitive_color:
 * @self: an #LrgDebugDraw
 * @index: a primitive index
 * @out_color: (out caller-allocates): the colour
 *
 * Returns: %TRUE when @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_debug_draw_get_primitive_color (LrgDebugDraw *self,
                                             guint         index,
                                             GrlColor     *out_color);

/**
 * lrg_debug_draw_get_primitive_point:
 * @self: an #LrgDebugDraw
 * @index: a primitive index
 * @which: which control point: for a line or arrow 0 = from, 1 = to;
 *   a box 0 = min, 1 = max; a sphere, circle, point, grid or label
 *   0 = centre/anchor; a capsule or cylinder 0 = a, 1 = b; a cone
 *   0 = apex, 1 = base; a polyline its points; an oriented box, axes
 *   or frustum 0 = the origin / centre of the near plane; 2D shapes
 *   their points with z = 0 (a rectangle: 0 = x, y and 1 = width,
 *   height)
 * @out_point: (out caller-allocates): the point
 *
 * Returns: %TRUE when the primitive has such a point
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_debug_draw_get_primitive_point (LrgDebugDraw *self,
                                             guint         index,
                                             guint         which,
                                             GrlVector3   *out_point);

/**
 * lrg_debug_draw_get_primitive_text:
 * @self: an #LrgDebugDraw
 * @index: a primitive index
 *
 * Returns: (nullable) (transfer none): a label's text, %NULL for other
 *   shapes
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_debug_draw_get_primitive_text (LrgDebugDraw *self,
                                                 guint         index);

/**
 * lrg_debug_draw_get_line_count:
 * @self: an #LrgDebugDraw
 * @depth_tested: %TRUE for the depth-tested list, %FALSE for the
 *   always-on-top one
 *
 * Tessellates the visible 3D primitives (enabled categories) on the
 * CPU, as the next draw will, and counts the line segments of one list
 * (thick ones included, before they become quads).
 *
 * Returns: the number of segments
 */
LRG_AVAILABLE_IN_ALL
guint lrg_debug_draw_get_line_count (LrgDebugDraw *self,
                                     gboolean      depth_tested);

/**
 * lrg_debug_draw_get_triangle_count:
 * @self: an #LrgDebugDraw
 * @depth_tested: %TRUE for the depth-tested list, %FALSE for the
 *   always-on-top one
 *
 * As lrg_debug_draw_get_line_count() for the solid shapes' triangles.
 *
 * Returns: the number of triangles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_debug_draw_get_triangle_count (LrgDebugDraw *self,
                                         gboolean      depth_tested);

/**
 * lrg_debug_draw_get_label_count:
 * @self: an #LrgDebugDraw
 *
 * Returns: the number of visible 3D text labels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_debug_draw_get_label_count (LrgDebugDraw *self);

/**
 * lrg_debug_draw_get_draw_calls:
 * @self: an #LrgDebugDraw
 *
 * Returns: the rlgl runs (GL_LINES / GL_TRIANGLES submissions) the last
 *   lrg_debug_draw_draw_3d() issued (0 headless): at most two per list
 */
LRG_AVAILABLE_IN_ALL
guint lrg_debug_draw_get_draw_calls (LrgDebugDraw *self);

G_END_DECLS
