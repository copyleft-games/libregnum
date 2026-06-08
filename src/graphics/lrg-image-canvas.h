/* lrg-image-canvas.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgImageCanvas - CPU-side dynamic texture canvas.
 *
 * Wraps a graylib GrlImage and exposes an engine-level API to draw
 * shapes, paths, and text, composite sub-images (Porter-Duff), apply
 * alpha masks, blur, and manage a transform stack.  Call
 * lrg_image_canvas_to_texture() to upload the finished image to the GPU
 * as a GrlTexture ready for rendering.
 *
 * Typical uses: dye systems, decals, drop-shadows, procedural icons.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_IMAGE_CANVAS (lrg_image_canvas_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgImageCanvas, lrg_image_canvas, LRG, IMAGE_CANVAS, GObject)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_image_canvas_new:
 * @width: canvas width in pixels (must be > 0)
 * @height: canvas height in pixels (must be > 0)
 * @bg: (nullable): background fill color, or %NULL for transparent (RGBA8)
 *
 * Creates a new #LrgImageCanvas backed by a freshly allocated #GrlImage.
 * When @bg is %NULL the image is created with a fully transparent background.
 *
 * Returns: (transfer full): a new #LrgImageCanvas
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgImageCanvas *
lrg_image_canvas_new (gint             width,
                      gint             height,
                      const GrlColor  *bg);

/**
 * lrg_image_canvas_new_for_image:
 * @image: an existing #GrlImage to wrap
 *
 * Creates a new #LrgImageCanvas that takes a reference to @image.  The
 * canvas draws into the same underlying pixel buffer; ownership of @image
 * is shared via the GObject reference count.
 *
 * Returns: (transfer full): a new #LrgImageCanvas
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgImageCanvas *
lrg_image_canvas_new_for_image (GrlImage *image);

/* ==========================================================================
 * Access
 * ========================================================================== */

/**
 * lrg_image_canvas_get_image:
 * @self: an #LrgImageCanvas
 *
 * Returns the underlying #GrlImage.  Use this for operations not yet
 * exposed by the canvas API.  The returned object is owned by the canvas.
 *
 * Returns: (transfer none): the #GrlImage
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_image_canvas_get_image (LrgImageCanvas *self);

/**
 * lrg_image_canvas_to_texture:
 * @self: an #LrgImageCanvas
 *
 * Uploads the current pixel data to the GPU and returns a new #GrlTexture.
 * The canvas continues to be usable after this call; further drawing will
 * not affect the returned texture.
 *
 * Requires an active rendering context (window must be open).
 *
 * Returns: (transfer full): a new #GrlTexture, or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlTexture *
lrg_image_canvas_to_texture (LrgImageCanvas *self);

/* ==========================================================================
 * State Setters
 * ========================================================================== */

/**
 * lrg_image_canvas_set_blend_mode:
 * @self: an #LrgImageCanvas
 * @mode: the blend mode to use for subsequent draw operations
 *
 * Sets the blend mode used when drawing primitives onto the canvas.
 * The default is %GRL_IMAGE_BLEND_REPLACE (overwrite).
 * Use %GRL_IMAGE_BLEND_OVER for alpha compositing, or %GRL_IMAGE_BLEND_ADD
 * for additive glow effects.  Non-REPLACE modes require an RGBA8 image.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_set_blend_mode (LrgImageCanvas    *self,
                                  GrlImageBlendMode  mode);

/**
 * lrg_image_canvas_set_blend_color_space:
 * @self: an #LrgImageCanvas
 * @color_space: the color space for blending
 *
 * Sets the color space used during blending.  Use
 * %GRL_IMAGE_COLOR_SPACE_LINEAR for physically correct blending.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_set_blend_color_space (LrgImageCanvas       *self,
                                         GrlImageColorSpace    color_space);

/**
 * lrg_image_canvas_set_antialias:
 * @self: an #LrgImageCanvas
 * @enabled: %TRUE to enable anti-aliasing
 *
 * Enables or disables anti-aliasing for subsequent draw operations.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_set_antialias (LrgImageCanvas *self,
                                 gboolean        enabled);

/**
 * lrg_image_canvas_set_clip_rect:
 * @self: an #LrgImageCanvas
 * @rect: (nullable): clip rectangle in image coordinates, or %NULL to clear
 *
 * Sets the clipping rectangle.  Subsequent draw calls only affect pixels
 * within @rect.  Pass %NULL to disable clipping.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_set_clip_rect (LrgImageCanvas      *self,
                                 const GrlRectangle  *rect);

/* ==========================================================================
 * Transform Stack
 * ========================================================================== */

/**
 * lrg_image_canvas_save:
 * @self: an #LrgImageCanvas
 *
 * Pushes the current transform matrix onto the stack.  Pair with
 * lrg_image_canvas_restore() to scope temporary transforms.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_save (LrgImageCanvas *self);

/**
 * lrg_image_canvas_restore:
 * @self: an #LrgImageCanvas
 *
 * Pops the most recently pushed transform matrix.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_restore (LrgImageCanvas *self);

/**
 * lrg_image_canvas_translate:
 * @self: an #LrgImageCanvas
 * @tx: translation along the X axis in pixels
 * @ty: translation along the Y axis in pixels
 *
 * Appends a translation to the current transform matrix.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_translate (LrgImageCanvas *self,
                              gfloat          tx,
                              gfloat          ty);

/**
 * lrg_image_canvas_scale:
 * @self: an #LrgImageCanvas
 * @sx: scale factor along the X axis
 * @sy: scale factor along the Y axis
 *
 * Appends a scale to the current transform matrix.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_scale (LrgImageCanvas *self,
                         gfloat          sx,
                         gfloat          sy);

/**
 * lrg_image_canvas_rotate:
 * @self: an #LrgImageCanvas
 * @radians: clockwise rotation angle in radians
 *
 * Appends a rotation to the current transform matrix.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_rotate (LrgImageCanvas *self,
                          gfloat          radians);

/**
 * lrg_image_canvas_reset_transform:
 * @self: an #LrgImageCanvas
 *
 * Resets the current transform to the identity matrix without affecting
 * the saved stack.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_reset_transform (LrgImageCanvas *self);

/* ==========================================================================
 * Drawing Primitives
 * ========================================================================== */

/**
 * lrg_image_canvas_clear:
 * @self: an #LrgImageCanvas
 * @color: the fill color
 *
 * Fills the entire canvas with @color, ignoring the current clip rectangle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_clear (LrgImageCanvas *self,
                          const GrlColor *color);

/**
 * lrg_image_canvas_fill_circle:
 * @self: an #LrgImageCanvas
 * @cx: circle centre X coordinate
 * @cy: circle centre Y coordinate
 * @radius: circle radius in pixels
 * @color: fill color
 *
 * Draws a filled circle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_fill_circle (LrgImageCanvas *self,
                               gint            cx,
                               gint            cy,
                               gint            radius,
                               const GrlColor *color);

/**
 * lrg_image_canvas_stroke_circle:
 * @self: an #LrgImageCanvas
 * @cx: circle centre X coordinate
 * @cy: circle centre Y coordinate
 * @radius: circle radius in pixels
 * @thickness: stroke width in pixels
 * @color: stroke color
 *
 * Draws the outline of a circle with the given stroke thickness.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_stroke_circle (LrgImageCanvas *self,
                                  gint            cx,
                                  gint            cy,
                                  gint            radius,
                                  gint            thickness,
                                  const GrlColor *color);

/**
 * lrg_image_canvas_fill_rect:
 * @self: an #LrgImageCanvas
 * @x: left edge of the rectangle
 * @y: top edge of the rectangle
 * @width: rectangle width in pixels
 * @height: rectangle height in pixels
 * @color: fill color
 *
 * Draws a filled axis-aligned rectangle.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_fill_rect (LrgImageCanvas *self,
                              gint            x,
                              gint            y,
                              gint            width,
                              gint            height,
                              const GrlColor *color);

/**
 * lrg_image_canvas_draw_line:
 * @self: an #LrgImageCanvas
 * @x1: X coordinate of the start point
 * @y1: Y coordinate of the start point
 * @x2: X coordinate of the end point
 * @y2: Y coordinate of the end point
 * @thickness: line thickness in pixels
 * @color: line color
 *
 * Draws an anti-aliased line with the given thickness.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_draw_line (LrgImageCanvas *self,
                              gint            x1,
                              gint            y1,
                              gint            x2,
                              gint            y2,
                              gint            thickness,
                              const GrlColor *color);

/**
 * lrg_image_canvas_fill_polygon:
 * @self: an #LrgImageCanvas
 * @points: (array length=n_points): array of GrlVector2 polygon vertices
 * @n_points: number of vertices
 * @color: fill color
 *
 * Draws a filled convex polygon.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_fill_polygon (LrgImageCanvas  *self,
                                const GrlVector2 *points,
                                gint              n_points,
                                const GrlColor   *color);

/**
 * lrg_image_canvas_fill_path:
 * @self: an #LrgImageCanvas
 * @path: the #GrlPath to fill
 * @fill_rule: the fill rule (even-odd or non-zero winding)
 * @color: fill color
 *
 * Fills a path using the specified fill rule.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_fill_path (LrgImageCanvas *self,
                              GrlPath        *path,
                              GrlFillRule     fill_rule,
                              const GrlColor *color);

/**
 * lrg_image_canvas_stroke_path:
 * @self: an #LrgImageCanvas
 * @path: the #GrlPath to stroke
 * @thickness: stroke width in pixels
 * @color: stroke color
 *
 * Strokes a path with the given thickness.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_stroke_path (LrgImageCanvas *self,
                               GrlPath        *path,
                               gint            thickness,
                               const GrlColor *color);

/**
 * lrg_image_canvas_draw_text:
 * @self: an #LrgImageCanvas
 * @font: the #GrlImageFont to render with
 * @text: UTF-8 encoded text string
 * @x: X position in canvas coordinates
 * @y: Y position in canvas coordinates
 * @font_size: font size in pixels
 * @color: text color
 *
 * Draws @text at (@x, @y) using the supplied TrueType font.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_draw_text (LrgImageCanvas *self,
                              GrlImageFont   *font,
                              const gchar    *text,
                              gint            x,
                              gint            y,
                              gfloat          font_size,
                              const GrlColor *color);

/**
 * lrg_image_canvas_draw_gradient_rect:
 * @self: an #LrgImageCanvas
 * @x: left edge of the rectangle
 * @y: top edge of the rectangle
 * @width: rectangle width in pixels
 * @height: rectangle height in pixels
 * @color_a: first gradient color (top or left, depending on @axis)
 * @color_b: second gradient color (bottom or right, depending on @axis)
 * @axis: gradient direction (%GRL_GRADIENT_AXIS_VERTICAL or
 *        %GRL_GRADIENT_AXIS_HORIZONTAL)
 *
 * Draws a rectangle filled with a linear gradient along @axis.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_draw_gradient_rect (LrgImageCanvas *self,
                                      gint            x,
                                      gint            y,
                                      gint            width,
                                      gint            height,
                                      const GrlColor *color_a,
                                      const GrlColor *color_b,
                                      GrlGradientAxis axis);

/**
 * lrg_image_canvas_draw_gradient_radial:
 * @self: an #LrgImageCanvas
 * @cx: centre X coordinate
 * @cy: centre Y coordinate
 * @radius: radius of the gradient circle in pixels
 * @inner_color: color at the centre
 * @outer_color: color at the outer edge
 *
 * Draws a radial gradient from @inner_color at the centre to @outer_color
 * at @radius pixels from the centre.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_draw_gradient_radial (LrgImageCanvas *self,
                                        gint            cx,
                                        gint            cy,
                                        gint            radius,
                                        const GrlColor *inner_color,
                                        const GrlColor *outer_color);

/* ==========================================================================
 * Compositing
 * ========================================================================== */

/**
 * lrg_image_canvas_composite:
 * @self: an #LrgImageCanvas (destination)
 * @src: the source #GrlImage to composite onto the canvas
 * @op: the Porter-Duff compositing operator
 * @dx: X offset in the destination canvas
 * @dy: Y offset in the destination canvas
 *
 * Composites @src onto the canvas at (@dx, @dy) using the Porter-Duff
 * operator @op.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_composite (LrgImageCanvas   *self,
                              GrlImage         *src,
                              GrlPorterDuffOp   op,
                              gint              dx,
                              gint              dy);

/**
 * lrg_image_canvas_apply_mask:
 * @self: an #LrgImageCanvas
 * @mask: an alpha mask #GrlImage (created with grl_image_new_mask())
 * @ox: X offset of the mask relative to the canvas
 * @oy: Y offset of the mask relative to the canvas
 *
 * Applies an alpha mask to the canvas.  Pixels where the mask is
 * transparent become transparent on the canvas.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_apply_mask (LrgImageCanvas *self,
                               GrlImage       *mask,
                               gint            ox,
                               gint            oy);

/**
 * lrg_image_canvas_blur:
 * @self: an #LrgImageCanvas
 * @radius: blur radius in pixels
 *
 * Applies a box blur with the given radius to the entire canvas.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_blur (LrgImageCanvas *self,
                        gint            radius);

/**
 * lrg_image_canvas_drop_shadow:
 * @self: an #LrgImageCanvas
 * @silhouette: a #GrlImage whose alpha channel defines the shadow shape
 * @dx: horizontal shadow offset in pixels (positive = right)
 * @dy: vertical shadow offset in pixels (positive = down)
 * @blur_radius: softness of the shadow edge in pixels
 * @shadow_color: shadow color (alpha is multiplied per-pixel)
 *
 * Convenience function that composites a soft, coloured drop-shadow
 * behind the existing canvas content.
 *
 * Internally this:
 * 1. Copies @silhouette and tints it with @shadow_color.
 * 2. Applies grl_image_blur_box() to soften the edges.
 * 3. Composites the blurred shadow BEHIND the current canvas content
 *    using %GRL_PORTER_DUFF_DST_OVER so the existing pixels remain on top.
 *
 * The shadow is positioned at (@dx, @dy) relative to the origin of
 * @silhouette on the canvas.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_image_canvas_drop_shadow (LrgImageCanvas *self,
                                GrlImage       *silhouette,
                                gint            dx,
                                gint            dy,
                                gint            blur_radius,
                                const GrlColor *shadow_color);

G_END_DECLS
