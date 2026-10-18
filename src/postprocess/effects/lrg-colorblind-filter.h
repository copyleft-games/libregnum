/* lrg-colorblind-filter.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Colorblind accessibility filter.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-post-effect.h"
#include "../../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLORBLIND_FILTER (lrg_colorblind_filter_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgColorblindFilter, lrg_colorblind_filter, LRG, COLORBLIND_FILTER, LrgPostEffect)

/**
 * lrg_colorblind_filter_new:
 *
 * Creates a new colorblind filter.
 *
 * Returns: (transfer full): A new #LrgColorblindFilter
 */
LRG_AVAILABLE_IN_ALL
LrgColorblindFilter *   lrg_colorblind_filter_new           (void);

/**
 * lrg_colorblind_filter_new_with_type:
 * @filter_type: The colorblind type to simulate/correct
 *
 * Creates a new colorblind filter with a specific type.
 *
 * Returns: (transfer full): A new #LrgColorblindFilter
 */
LRG_AVAILABLE_IN_ALL
LrgColorblindFilter *   lrg_colorblind_filter_new_with_type (LrgColorblindType   filter_type);

/**
 * lrg_colorblind_filter_get_filter_type:
 * @self: A #LrgColorblindFilter
 *
 * Gets the current filter type.
 *
 * Returns: The #LrgColorblindType
 */
LRG_AVAILABLE_IN_ALL
LrgColorblindType       lrg_colorblind_filter_get_filter_type (LrgColorblindFilter *self);

/**
 * lrg_colorblind_filter_set_filter_type:
 * @self: A #LrgColorblindFilter
 * @filter_type: The colorblind type
 *
 * Sets the colorblind filter type.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_colorblind_filter_set_filter_type (LrgColorblindFilter *self,
                                                                LrgColorblindType    filter_type);

/**
 * lrg_colorblind_filter_get_mode:
 * @self: A #LrgColorblindFilter
 *
 * Gets the filter mode.
 *
 * Returns: The #LrgColorblindMode
 */
LRG_AVAILABLE_IN_ALL
LrgColorblindMode       lrg_colorblind_filter_get_mode      (LrgColorblindFilter *self);

/**
 * lrg_colorblind_filter_set_mode:
 * @self: A #LrgColorblindFilter
 * @mode: The filter mode
 *
 * Sets whether to simulate or correct colorblindness.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_colorblind_filter_set_mode      (LrgColorblindFilter *self,
                                                             LrgColorblindMode    mode);

/**
 * lrg_colorblind_filter_get_strength:
 * @self: A #LrgColorblindFilter
 *
 * Gets the filter strength.
 *
 * Returns: The strength (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat                  lrg_colorblind_filter_get_strength  (LrgColorblindFilter *self);

/**
 * lrg_colorblind_filter_set_strength:
 * @self: A #LrgColorblindFilter
 * @strength: The filter strength (0.0 to 1.0)
 *
 * Sets the filter strength.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_colorblind_filter_set_strength  (LrgColorblindFilter *self,
                                                             gfloat               strength);

/**
 * lrg_colorblind_filter_get_matrix:
 * @self: A #LrgColorblindFilter
 * @matrix: (out caller-allocates) (array fixed-size=9): the 3x3 matrix,
 *   row-major
 *
 * The matrix the filter applies to a colour in linear light, with the
 * mode and the strength folded in.  Simulation uses the full-severity
 * matrices of Machado, Oliveira and Fernandes (2009); correction is
 * daltonization: the error between a colour and its simulation is
 * shifted into the channels the viewer still tells apart (red and green
 * deficiencies push it into green and blue, tritanopia into red and
 * green), so the matrix is I + D (I - S).  Achromatopsia has nothing left
 * to shift into: its correction is the identity.  The strength mixes the
 * matrix with the identity; %LRG_COLORBLIND_NONE is the identity.
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_colorblind_filter_get_matrix    (LrgColorblindFilter *self,
                                                             gfloat              *matrix);

/**
 * lrg_colorblind_filter_is_identity:
 * @self: A #LrgColorblindFilter
 *
 * Whether the filter leaves every colour unchanged: no type, a strength
 * of 0, correction of achromatopsia, or an effect intensity of 0.
 *
 * Returns: %TRUE when lrg_colorblind_filter_apply_rgb() is the identity
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_colorblind_filter_is_identity   (LrgColorblindFilter *self);

/**
 * lrg_colorblind_filter_apply_rgb:
 * @self: A #LrgColorblindFilter
 * @in: (array fixed-size=3): an sRGB colour, each channel 0.0 to 1.0
 * @out: (out caller-allocates) (array fixed-size=3): the filtered
 *   colour, clamped to 0.0 to 1.0 (may be @in)
 *
 * The filter's reference on the CPU, the fragment shader's steps
 * (lrg_colorblind_filter_get_fragment_shader()): the colour is taken to
 * linear light (power 2.2), multiplied by lrg_colorblind_filter_get_matrix(),
 * clamped and taken back (power 1 / 2.2), then mixed with the original by
 * the effect's intensity.  Used by tests and by games that filter
 * colours they draw themselves (UI fills and text).
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_colorblind_filter_apply_rgb     (LrgColorblindFilter *self,
                                                             const gfloat        *in,
                                                             gfloat              *out);

/**
 * lrg_colorblind_filter_get_fragment_shader:
 *
 * The GLSL 330 fragment shader of the filter, for raylib's default
 * vertex shader, with the uniforms row0, row1, row2 (the matrix's rows)
 * and intensity.
 *
 * Returns: (transfer none): the shader source
 */
LRG_AVAILABLE_IN_ALL
const gchar *           lrg_colorblind_filter_get_fragment_shader (void);

/**
 * lrg_colorblind_filter_get_shader:
 * @self: A #LrgColorblindFilter
 *
 * The compiled shader, built on first use.  Needs a window (an OpenGL
 * context); without one, or when it does not compile, %NULL.
 *
 * Returns: (transfer none) (nullable): the shader
 */
LRG_AVAILABLE_IN_ALL
GrlShader *             lrg_colorblind_filter_get_shader    (LrgColorblindFilter *self);

/**
 * lrg_colorblind_filter_draw_texture:
 * @self: A #LrgColorblindFilter
 * @texture: the texture to draw (a render texture's colour attachment)
 * @source: the part of @texture, in texels (a negative height flips it)
 * @dest: where to draw it, in pixels of the current target
 *
 * Draws @texture filtered into the current target.  An identity filter
 * draws it plainly; without a shader it is drawn plainly and %FALSE is
 * returned.
 *
 * Returns: %TRUE when the filter was applied (or was the identity)
 */
LRG_AVAILABLE_IN_ALL
gboolean                lrg_colorblind_filter_draw_texture  (LrgColorblindFilter *self,
                                                             GrlTexture          *texture,
                                                             const GrlRectangle  *source,
                                                             const GrlRectangle  *dest);

G_END_DECLS
