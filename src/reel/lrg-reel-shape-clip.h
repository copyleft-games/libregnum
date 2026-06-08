/* lrg-reel-shape-clip.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelShapeClip - a reel clip that draws a filled and/or stroked shape.
 *
 * #LrgReelShapeClip renders one of the shapes enumerated by #LrgReelShapeKind
 * onto the clip's #LrgImageCanvas every frame.  Shapes are drawn in frame
 * coordinates with an optional fill color, stroke color, and stroke width.
 * Transform and opacity compositing are handled by the parent #LrgReelClip
 * machinery before the render vfunc is called.
 *
 * The geometry of each shape is controlled by a small set of named properties:
 *
 *   - @shape-x / @shape-y: top-left origin for bounding-box shapes (rect,
 *     rounded-rect, ellipse, triangle) or line start point.
 *   - @shape-width / @shape-height: bounding box dimensions.
 *   - @shape-radius: radius for %LRG_REEL_SHAPE_CIRCLE.
 *   - @corner-radius: rounding radius for %LRG_REEL_SHAPE_ROUNDED_RECT.
 *   - @shape-x2 / @shape-y2: line end point for %LRG_REEL_SHAPE_LINE.
 *   - @point-count: number of star points for %LRG_REEL_SHAPE_STAR (default 5).
 *   - @inner-radius-ratio: inner/outer ratio for stars (default 0.5).
 *
 * For %LRG_REEL_SHAPE_POLYGON use lrg_reel_shape_clip_set_points().
 * For %LRG_REEL_SHAPE_PATH use lrg_reel_shape_clip_set_path().
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-reel-clip.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_SHAPE_CLIP (lrg_reel_shape_clip_get_type ())

/**
 * LrgReelShapeClip:
 *
 * A #LrgReelClip subclass that draws a filled and/or stroked vector shape.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelShapeClip, lrg_reel_shape_clip,
                      LRG, REEL_SHAPE_CLIP, LrgReelClip)

/* ==========================================================================
 * Constructors
 * ========================================================================== */

/**
 * lrg_reel_shape_clip_new:
 * @kind: the shape variant to draw.
 *
 * Creates a new #LrgReelShapeClip of the given @kind.  The fill color defaults
 * to opaque white; no stroke is drawn by default.
 *
 * Returns: (transfer full): a new #LrgReelShapeClip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelShapeClip *
lrg_reel_shape_clip_new (LrgReelShapeKind kind);

/**
 * lrg_reel_shape_clip_new_rect:
 * @x: left edge of the rectangle in frame coordinates.
 * @y: top edge of the rectangle in frame coordinates.
 * @w: rectangle width in pixels.
 * @h: rectangle height in pixels.
 *
 * Convenience constructor: creates a %LRG_REEL_SHAPE_RECT clip with the given
 * bounding box.  Fill color is opaque white; no stroke.
 *
 * Returns: (transfer full): a new #LrgReelShapeClip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelShapeClip *
lrg_reel_shape_clip_new_rect (gint x,
                               gint y,
                               gint w,
                               gint h);

/**
 * lrg_reel_shape_clip_new_circle:
 * @cx: centre X coordinate in frame coordinates.
 * @cy: centre Y coordinate in frame coordinates.
 * @radius: circle radius in pixels.
 *
 * Convenience constructor: creates a %LRG_REEL_SHAPE_CIRCLE clip.  Fill color
 * is opaque white; no stroke.
 *
 * Returns: (transfer full): a new #LrgReelShapeClip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelShapeClip *
lrg_reel_shape_clip_new_circle (gint cx,
                                 gint cy,
                                 gint radius);

/**
 * lrg_reel_shape_clip_new_line:
 * @x1: X coordinate of the start point.
 * @y1: Y coordinate of the start point.
 * @x2: X coordinate of the end point.
 * @y2: Y coordinate of the end point.
 *
 * Convenience constructor: creates a %LRG_REEL_SHAPE_LINE clip.  The stroke
 * color defaults to opaque white; stroke width defaults to 1.  Fill color
 * alpha is 0, so no fill is drawn.
 *
 * Returns: (transfer full): a new #LrgReelShapeClip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelShapeClip *
lrg_reel_shape_clip_new_line (gint x1,
                               gint y1,
                               gint x2,
                               gint y2);

/* ==========================================================================
 * Geometry accessors
 * ========================================================================== */

/**
 * lrg_reel_shape_clip_get_kind:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the shape kind.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelShapeKind
lrg_reel_shape_clip_get_kind (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_get_shape_x:
 * @self: an #LrgReelShapeClip.
 *
 * Returns the @shape-x property: the X origin of the bounding box for
 * %LRG_REEL_SHAPE_RECT, %LRG_REEL_SHAPE_ROUNDED_RECT,
 * %LRG_REEL_SHAPE_ELLIPSE, and %LRG_REEL_SHAPE_TRIANGLE shapes, or the X
 * coordinate of the line start for %LRG_REEL_SHAPE_LINE.
 *
 * Returns: the shape X coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_shape_clip_get_shape_x (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_shape_x:
 * @self: an #LrgReelShapeClip.
 * @x: the new shape X coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_shape_x (LrgReelShapeClip *self,
                                  gint              x);

/**
 * lrg_reel_shape_clip_get_shape_y:
 * @self: an #LrgReelShapeClip.
 *
 * Returns the @shape-y property.  See lrg_reel_shape_clip_get_shape_x() for
 * semantic details.
 *
 * Returns: the shape Y coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_shape_clip_get_shape_y (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_shape_y:
 * @self: an #LrgReelShapeClip.
 * @y: the new shape Y coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_shape_y (LrgReelShapeClip *self,
                                  gint              y);

/**
 * lrg_reel_shape_clip_get_shape_width:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the bounding-box width in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_shape_clip_get_shape_width (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_shape_width:
 * @self: an #LrgReelShapeClip.
 * @width: the new width in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_shape_width (LrgReelShapeClip *self,
                                      gint              width);

/**
 * lrg_reel_shape_clip_get_shape_height:
 * @self: an #LrgReelShapeClip.
 *
 * Returns: the bounding-box height in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_shape_clip_get_shape_height (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_shape_height:
 * @self: an #LrgReelShapeClip.
 * @height: the new height in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_shape_height (LrgReelShapeClip *self,
                                       gint              height);

/**
 * lrg_reel_shape_clip_get_shape_radius:
 * @self: an #LrgReelShapeClip.
 *
 * Returns the @shape-radius property: the radius for %LRG_REEL_SHAPE_CIRCLE
 * shapes, and also the outer radius for %LRG_REEL_SHAPE_STAR shapes.
 *
 * Returns: the radius in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_shape_clip_get_shape_radius (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_shape_radius:
 * @self: an #LrgReelShapeClip.
 * @radius: the new radius in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_shape_radius (LrgReelShapeClip *self,
                                       gint              radius);

/**
 * lrg_reel_shape_clip_get_corner_radius:
 * @self: an #LrgReelShapeClip.
 *
 * Returns the @corner-radius property: the corner rounding radius in pixels
 * used by %LRG_REEL_SHAPE_ROUNDED_RECT.
 *
 * Returns: the corner radius in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_shape_clip_get_corner_radius (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_corner_radius:
 * @self: an #LrgReelShapeClip.
 * @corner_radius: the new corner radius in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_corner_radius (LrgReelShapeClip *self,
                                        gint              corner_radius);

/**
 * lrg_reel_shape_clip_get_shape_x2:
 * @self: an #LrgReelShapeClip.
 *
 * Returns the @shape-x2 property: X coordinate of the end point for
 * %LRG_REEL_SHAPE_LINE.
 *
 * Returns: the line end X coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_shape_clip_get_shape_x2 (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_shape_x2:
 * @self: an #LrgReelShapeClip.
 * @x2: the new line end X coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_shape_x2 (LrgReelShapeClip *self,
                                   gint              x2);

/**
 * lrg_reel_shape_clip_get_shape_y2:
 * @self: an #LrgReelShapeClip.
 *
 * Returns the @shape-y2 property: Y coordinate of the end point for
 * %LRG_REEL_SHAPE_LINE.
 *
 * Returns: the line end Y coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_shape_clip_get_shape_y2 (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_shape_y2:
 * @self: an #LrgReelShapeClip.
 * @y2: the new line end Y coordinate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_shape_y2 (LrgReelShapeClip *self,
                                   gint              y2);

/**
 * lrg_reel_shape_clip_get_point_count:
 * @self: an #LrgReelShapeClip.
 *
 * Returns the @point-count property: the number of points on a
 * %LRG_REEL_SHAPE_STAR shape.  Defaults to 5.
 *
 * Returns: number of star points.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_shape_clip_get_point_count (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_point_count:
 * @self: an #LrgReelShapeClip.
 * @point_count: the new number of star points (>= 2).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_point_count (LrgReelShapeClip *self,
                                      gint              point_count);

/**
 * lrg_reel_shape_clip_get_inner_radius_ratio:
 * @self: an #LrgReelShapeClip.
 *
 * Returns the @inner-radius-ratio property: the ratio of the inner notch
 * radius to the outer radius for %LRG_REEL_SHAPE_STAR shapes.  A value of
 * 0.5 (the default) produces a classic five-pointed star; values closer to
 * 1.0 produce a nearly regular polygon.
 *
 * Returns: the inner radius ratio in [0.0, 1.0].
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_shape_clip_get_inner_radius_ratio (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_inner_radius_ratio:
 * @self: an #LrgReelShapeClip.
 * @ratio: the new inner radius ratio in [0.0, 1.0].
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_inner_radius_ratio (LrgReelShapeClip *self,
                                             gdouble           ratio);

/* ==========================================================================
 * Polygon / path geometry
 * ========================================================================== */

/**
 * lrg_reel_shape_clip_set_points:
 * @self: an #LrgReelShapeClip.
 * @points: (array length=n_points): vertex array for a
 *   %LRG_REEL_SHAPE_POLYGON shape.
 * @n_points: number of vertices in @points.
 *
 * Copies @points into the clip.  The points are used when the kind is
 * %LRG_REEL_SHAPE_POLYGON; for other kinds they are ignored.  At least 3
 * points are required to produce visible output.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_points (LrgReelShapeClip  *self,
                                 const GrlVector2  *points,
                                 gint               n_points);

/**
 * lrg_reel_shape_clip_set_path:
 * @self: an #LrgReelShapeClip.
 * @path: (nullable) (transfer none): a #GrlPath to use for
 *   %LRG_REEL_SHAPE_PATH, or %NULL to clear.
 *
 * Stores a reference to @path.  The path is used when the kind is
 * %LRG_REEL_SHAPE_PATH; for other kinds it is ignored.  Pass %NULL to
 * clear the current path.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_path (LrgReelShapeClip *self,
                               GrlPath          *path);

/* ==========================================================================
 * Style accessors
 * ========================================================================== */

/**
 * lrg_reel_shape_clip_get_fill_color:
 * @self: an #LrgReelShapeClip.
 * @out_color: (out caller-allocates): return location for the fill color.
 *
 * Copies the current fill color into @out_color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_get_fill_color (LrgReelShapeClip *self,
                                     GrlColor         *out_color);

/**
 * lrg_reel_shape_clip_set_fill_color:
 * @self: an #LrgReelShapeClip.
 * @color: the new fill color.
 *
 * Sets the fill color.  If @color has alpha 0 no fill is drawn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_fill_color (LrgReelShapeClip *self,
                                     const GrlColor   *color);

/**
 * lrg_reel_shape_clip_get_stroke_color:
 * @self: an #LrgReelShapeClip.
 * @out_color: (out caller-allocates): return location for the stroke color.
 *
 * Copies the current stroke color into @out_color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_get_stroke_color (LrgReelShapeClip *self,
                                       GrlColor         *out_color);

/**
 * lrg_reel_shape_clip_set_stroke_color:
 * @self: an #LrgReelShapeClip.
 * @color: the new stroke color.
 *
 * Sets the stroke color.  The stroke is only drawn when the @stroke-width
 * property is > 0 and @color has alpha > 0.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_stroke_color (LrgReelShapeClip *self,
                                       const GrlColor   *color);

/**
 * lrg_reel_shape_clip_get_stroke_width:
 * @self: an #LrgReelShapeClip.
 *
 * Returns the @stroke-width property.  A value of 0 (the default) disables
 * the stroke entirely.
 *
 * Returns: the stroke width in pixels.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_shape_clip_get_stroke_width (LrgReelShapeClip *self);

/**
 * lrg_reel_shape_clip_set_stroke_width:
 * @self: an #LrgReelShapeClip.
 * @stroke_width: the new stroke width in pixels (0 = no stroke).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_shape_clip_set_stroke_width (LrgReelShapeClip *self,
                                       gint              stroke_width);

G_END_DECLS
