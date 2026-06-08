/* lrg-reel-gradient-clip.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelGradientClip - a reel clip that fills the frame with a gradient.
 *
 * Supports two modes: a linear gradient (horizontal or vertical axis) and a
 * radial gradient centered on the frame.  Construct via
 * lrg_reel_gradient_clip_new_linear() or lrg_reel_gradient_clip_new_radial().
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

#define LRG_TYPE_REEL_GRADIENT_CLIP (lrg_reel_gradient_clip_get_type ())

/**
 * LrgReelGradientClip:
 *
 * A #LrgReelClip subclass that fills the frame with a linear or radial
 * gradient.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelGradientClip, lrg_reel_gradient_clip,
                      LRG, REEL_GRADIENT_CLIP, LrgReelClip)

/**
 * lrg_reel_gradient_clip_new_linear:
 * @start: the gradient color at the start edge.
 * @end: the gradient color at the end edge.
 * @axis: %GRL_GRADIENT_AXIS_HORIZONTAL or %GRL_GRADIENT_AXIS_VERTICAL.
 *
 * Creates a new #LrgReelGradientClip that fills the frame with a linear
 * gradient from @start to @end along @axis.
 *
 * For %GRL_GRADIENT_AXIS_HORIZONTAL, @start is on the left and @end on the
 * right.  For %GRL_GRADIENT_AXIS_VERTICAL, @start is at the top and @end at
 * the bottom.
 *
 * Returns: (transfer full): a new #LrgReelGradientClip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelGradientClip *
lrg_reel_gradient_clip_new_linear (const GrlColor  *start,
                                   const GrlColor  *end,
                                   GrlGradientAxis  axis);

/**
 * lrg_reel_gradient_clip_new_radial:
 * @inner: the gradient color at the center.
 * @outer: the gradient color at the edge.
 *
 * Creates a new #LrgReelGradientClip that fills the frame with a radial
 * gradient from @inner at the frame center to @outer at the frame edge.
 *
 * Returns: (transfer full): a new #LrgReelGradientClip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelGradientClip *
lrg_reel_gradient_clip_new_radial (const GrlColor *inner,
                                   const GrlColor *outer);

/**
 * lrg_reel_gradient_clip_get_is_radial:
 * @self: an #LrgReelGradientClip.
 *
 * Returns: %TRUE if the gradient is radial, %FALSE if it is linear.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_gradient_clip_get_is_radial (LrgReelGradientClip *self);

/**
 * lrg_reel_gradient_clip_get_axis:
 * @self: an #LrgReelGradientClip.
 *
 * Returns the gradient axis for linear gradients.  The value is undefined
 * for radial gradients; use lrg_reel_gradient_clip_get_is_radial() first.
 *
 * Returns: the #GrlGradientAxis in use.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlGradientAxis
lrg_reel_gradient_clip_get_axis (LrgReelGradientClip *self);

/**
 * lrg_reel_gradient_clip_set_axis:
 * @self: an #LrgReelGradientClip.
 * @axis: the new #GrlGradientAxis.
 *
 * Changes the axis for a linear gradient.  Has no effect on radial gradients.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gradient_clip_set_axis (LrgReelGradientClip *self,
                                  GrlGradientAxis      axis);

/**
 * lrg_reel_gradient_clip_get_start_color:
 * @self: an #LrgReelGradientClip.
 * @out_color: (out caller-allocates): return location for the color.
 *
 * Copies the start (or inner, for radial) color into @out_color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gradient_clip_get_start_color (LrgReelGradientClip *self,
                                         GrlColor            *out_color);

/**
 * lrg_reel_gradient_clip_set_start_color:
 * @self: an #LrgReelGradientClip.
 * @color: the new start (or inner) color.
 *
 * Sets the start color for linear gradients or the inner color for radial
 * gradients.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gradient_clip_set_start_color (LrgReelGradientClip *self,
                                         const GrlColor      *color);

/**
 * lrg_reel_gradient_clip_get_end_color:
 * @self: an #LrgReelGradientClip.
 * @out_color: (out caller-allocates): return location for the color.
 *
 * Copies the end (or outer, for radial) color into @out_color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gradient_clip_get_end_color (LrgReelGradientClip *self,
                                       GrlColor            *out_color);

/**
 * lrg_reel_gradient_clip_set_end_color:
 * @self: an #LrgReelGradientClip.
 * @color: the new end (or outer) color.
 *
 * Sets the end color for linear gradients or the outer color for radial
 * gradients.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gradient_clip_set_end_color (LrgReelGradientClip *self,
                                       const GrlColor      *color);

G_END_DECLS
