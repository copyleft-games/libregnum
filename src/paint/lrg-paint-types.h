/* lrg-paint-types.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Value types shared by #LrgPaintList and #LrgPaintRenderer.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgCornerRadii
 * ========================================================================== */

#define LRG_TYPE_CORNER_RADII (lrg_corner_radii_get_type ())

/**
 * LrgCornerRadii:
 * @top_left: radius of the top-left corner
 * @top_right: radius of the top-right corner
 * @bottom_right: radius of the bottom-right corner
 * @bottom_left: radius of the bottom-left corner
 *
 * Per-corner radii for rounded rectangles, in canvas units. Radii
 * larger than half the shorter side are clamped when drawn, so a
 * radius of %G_MAXFLOAT always gives a pill or circle.
 */
typedef struct
{
	gfloat top_left;
	gfloat top_right;
	gfloat bottom_right;
	gfloat bottom_left;
} LrgCornerRadii;

LRG_AVAILABLE_IN_ALL
GType lrg_corner_radii_get_type (void) G_GNUC_CONST;

/**
 * lrg_corner_radii_new:
 * @top_left: top-left radius
 * @top_right: top-right radius
 * @bottom_right: bottom-right radius
 * @bottom_left: bottom-left radius
 *
 * Returns: (transfer full): a new #LrgCornerRadii
 */
LRG_AVAILABLE_IN_ALL
LrgCornerRadii * lrg_corner_radii_new (gfloat top_left,
                                       gfloat top_right,
                                       gfloat bottom_right,
                                       gfloat bottom_left);

/**
 * lrg_corner_radii_copy:
 * @self: an #LrgCornerRadii
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgCornerRadii * lrg_corner_radii_copy (const LrgCornerRadii *self);

/**
 * lrg_corner_radii_free:
 * @self: (nullable): an #LrgCornerRadii
 */
LRG_AVAILABLE_IN_ALL
void lrg_corner_radii_free (LrgCornerRadii *self);

/**
 * lrg_corner_radii_init_uniform:
 * @self: (out caller-allocates): radii to set
 * @radius: radius for all four corners
 *
 * Sets all four corners to @radius.
 */
LRG_AVAILABLE_IN_ALL
void lrg_corner_radii_init_uniform (LrgCornerRadii *self,
                                    gfloat          radius);

/**
 * lrg_corner_radii_scale:
 * @self: an #LrgCornerRadii
 * @factor: multiplier
 *
 * Multiplies every radius by @factor in place.
 */
LRG_AVAILABLE_IN_ALL
void lrg_corner_radii_scale (LrgCornerRadii *self,
                             gfloat          factor);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgCornerRadii, lrg_corner_radii_free)

/* ==========================================================================
 * LrgPaintGradient
 * ========================================================================== */

#define LRG_TYPE_PAINT_GRADIENT (lrg_paint_gradient_get_type ())

/**
 * LrgPaintGradient:
 * @kind: %LRG_PAINT_GRADIENT_NONE, %LRG_PAINT_GRADIENT_LINEAR or
 *   %LRG_PAINT_GRADIENT_RADIAL
 * @start: colour at the start (linear) or centre (radial)
 * @end: colour at the end (linear) or edge (radial)
 * @angle: linear only: direction in degrees, 0 is left-to-right,
 *   90 is top-to-bottom
 * @center_x: radial only: centre as a fraction of the width (0..1)
 * @center_y: radial only: centre as a fraction of the height (0..1)
 * @radius: radial only: distance in canvas units where @end is reached;
 *   0 means half the rectangle's diagonal
 *
 * A two-stop gradient filling a rectangle. Linear gradients span the
 * rectangle's full extent along @angle, so a 0 degree gradient goes
 * from @start at the left edge to @end at the right edge.
 */
typedef struct
{
	LrgPaintGradientKind kind;
	GrlColor             start;
	GrlColor             end;
	gfloat               angle;
	gfloat               center_x;
	gfloat               center_y;
	gfloat               radius;
} LrgPaintGradient;

LRG_AVAILABLE_IN_ALL
GType lrg_paint_gradient_get_type (void) G_GNUC_CONST;

/**
 * lrg_paint_gradient_copy:
 * @self: an #LrgPaintGradient
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgPaintGradient * lrg_paint_gradient_copy (const LrgPaintGradient *self);

/**
 * lrg_paint_gradient_free:
 * @self: (nullable): an #LrgPaintGradient
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_gradient_free (LrgPaintGradient *self);

/**
 * lrg_paint_gradient_init_linear:
 * @self: (out caller-allocates): gradient to set
 * @start: start colour
 * @end: end colour
 * @angle: direction in degrees (0 = left to right, 90 = top to bottom)
 *
 * Sets up a linear gradient.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_gradient_init_linear (LrgPaintGradient *self,
                                     const GrlColor   *start,
                                     const GrlColor   *end,
                                     gfloat            angle);

/**
 * lrg_paint_gradient_init_radial:
 * @self: (out caller-allocates): gradient to set
 * @start: centre colour
 * @end: edge colour
 * @center_x: centre as a fraction of the width
 * @center_y: centre as a fraction of the height
 * @radius: distance where @end is reached, 0 for half the diagonal
 *
 * Sets up a radial gradient.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_gradient_init_radial (LrgPaintGradient *self,
                                     const GrlColor   *start,
                                     const GrlColor   *end,
                                     gfloat            center_x,
                                     gfloat            center_y,
                                     gfloat            radius);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPaintGradient, lrg_paint_gradient_free)

/* ==========================================================================
 * LrgPaintInstance
 * ========================================================================== */

#define LRG_TYPE_PAINT_INSTANCE (lrg_paint_instance_get_type ())

/**
 * LrgPaintInstance:
 * @rect: x0, y0, x1, y1 of the shape; for lines the two end points
 * @radii: corner radii, top-left, top-right, bottom-right, bottom-left
 * @clip: x0, y0, x1, y1 of the clip rectangle; pixels outside are discarded
 * @params: border width, auxiliary size (shadow blur or line
 *   thickness), #LrgPaintPrim, #LrgPaintGradientKind
 * @extra: gradient direction (linear: cos, sin), gradient centre and
 *   radius (radial: x, y in 0..1, radius), or texture coordinates
 *   (images: u0, v0, u1, v1)
 * @color: fill colour, or gradient start, straight-alpha RGBA8
 * @color2: gradient end colour, straight-alpha RGBA8
 * @border: border colour, straight-alpha RGBA8
 * @texture: GL texture id sampled by %LRG_PAINT_PRIM_IMAGE, 0 otherwise
 *
 * One shape as it is uploaded to the GPU. Every primitive the paint
 * renderer draws, including each glyph of text, is one instance; the
 * vertex shader expands it into a screen-space quad and the fragment
 * shader evaluates its signed distance field. Read-only to callers:
 * build instances through #LrgPaintList.
 */
typedef struct
{
	gfloat  rect[4];
	gfloat  radii[4];
	gfloat  clip[4];
	gfloat  params[4];
	gfloat  extra[4];
	guint8  color[4];
	guint8  color2[4];
	guint8  border[4];
	guint32 texture;
} LrgPaintInstance;

LRG_AVAILABLE_IN_ALL
GType lrg_paint_instance_get_type (void) G_GNUC_CONST;

/**
 * lrg_paint_instance_copy:
 * @self: an #LrgPaintInstance
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgPaintInstance * lrg_paint_instance_copy (const LrgPaintInstance *self);

/**
 * lrg_paint_instance_free:
 * @self: (nullable): an #LrgPaintInstance
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_instance_free (LrgPaintInstance *self);

/**
 * lrg_paint_instance_get_prim:
 * @self: an #LrgPaintInstance
 *
 * Returns: the primitive this instance draws
 */
LRG_AVAILABLE_IN_ALL
LrgPaintPrim lrg_paint_instance_get_prim (const LrgPaintInstance *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPaintInstance, lrg_paint_instance_free)

/* ==========================================================================
 * Colour helpers
 * ========================================================================== */

/**
 * lrg_paint_color_mix:
 * @a: first colour
 * @b: second colour
 * @t: blend factor, 0 gives @a and 1 gives @b
 * @out: (out caller-allocates): the blended colour
 *
 * Linearly blends two colours, alpha included. Used to cross-fade
 * state looks while springs move.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_color_mix (const GrlColor *a,
                          const GrlColor *b,
                          gfloat          t,
                          GrlColor       *out);

/**
 * lrg_paint_color_shade:
 * @color: base colour
 * @amount: -1..1; negative darkens towards black, positive lightens
 *   towards white
 * @out: (out caller-allocates): the shaded colour (alpha unchanged)
 *
 * Lightens or darkens a colour, for deriving hover and pressed shades.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_color_shade (const GrlColor *color,
                            gfloat          amount,
                            GrlColor       *out);

/**
 * lrg_paint_color_with_alpha:
 * @color: base colour
 * @alpha: new alpha, 0..1
 * @out: (out caller-allocates): @color with its alpha replaced
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_color_with_alpha (const GrlColor *color,
                                 gfloat          alpha,
                                 GrlColor       *out);

/**
 * lrg_paint_color_luminance:
 * @color: a colour
 *
 * Relative luminance as defined by WCAG 2.x, from 0 (black) to 1
 * (white).
 *
 * Returns: the relative luminance
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_paint_color_luminance (const GrlColor *color);

/**
 * lrg_paint_color_contrast:
 * @a: a colour
 * @b: another colour
 *
 * WCAG contrast ratio between two opaque colours, from 1 (identical)
 * to 21 (black on white). 4.5 is the minimum for body text.
 *
 * Returns: the contrast ratio
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_paint_color_contrast (const GrlColor *a,
                                 const GrlColor *b);

G_END_DECLS
