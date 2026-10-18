/* lrg-color-grade.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Color grading post-processing effect.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-post-effect.h"
#include "../lrg-color-lut.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLOR_GRADE (lrg_color_grade_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgColorGrade, lrg_color_grade, LRG, COLOR_GRADE, LrgPostEffect)

/**
 * lrg_color_grade_new:
 *
 * Creates a new color grading effect.
 *
 * Returns: (transfer full): A new #LrgColorGrade
 */
LRG_AVAILABLE_IN_ALL
LrgColorGrade *     lrg_color_grade_new             (void);

/**
 * lrg_color_grade_get_exposure:
 * @self: A #LrgColorGrade
 *
 * Gets the exposure adjustment.
 *
 * Returns: The exposure value
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_color_grade_get_exposure    (LrgColorGrade  *self);

/**
 * lrg_color_grade_set_exposure:
 * @self: A #LrgColorGrade
 * @exposure: Exposure adjustment (-5.0 to 5.0)
 *
 * Sets the exposure adjustment.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_set_exposure    (LrgColorGrade  *self,
                                                     gfloat          exposure);

/**
 * lrg_color_grade_get_contrast:
 * @self: A #LrgColorGrade
 *
 * Gets the contrast.
 *
 * Returns: The contrast value
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_color_grade_get_contrast    (LrgColorGrade  *self);

/**
 * lrg_color_grade_set_contrast:
 * @self: A #LrgColorGrade
 * @contrast: Contrast (0.0 to 2.0, 1.0 = neutral)
 *
 * Sets the contrast.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_set_contrast    (LrgColorGrade  *self,
                                                     gfloat          contrast);

/**
 * lrg_color_grade_get_saturation:
 * @self: A #LrgColorGrade
 *
 * Gets the saturation.
 *
 * Returns: The saturation value
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_color_grade_get_saturation  (LrgColorGrade  *self);

/**
 * lrg_color_grade_set_saturation:
 * @self: A #LrgColorGrade
 * @saturation: Saturation (0.0 to 2.0, 1.0 = neutral)
 *
 * Sets the saturation.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_set_saturation  (LrgColorGrade  *self,
                                                     gfloat          saturation);

/**
 * lrg_color_grade_get_temperature:
 * @self: A #LrgColorGrade
 *
 * Gets the color temperature adjustment.
 *
 * Returns: The temperature value
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_color_grade_get_temperature (LrgColorGrade  *self);

/**
 * lrg_color_grade_set_temperature:
 * @self: A #LrgColorGrade
 * @temperature: Temperature (-1.0 to 1.0, negative = cool, positive = warm)
 *
 * Sets the color temperature.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_set_temperature (LrgColorGrade  *self,
                                                     gfloat          temperature);

/**
 * lrg_color_grade_get_tint:
 * @self: A #LrgColorGrade
 *
 * Gets the tint (magenta-green) adjustment.
 *
 * Returns: The tint value
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_color_grade_get_tint        (LrgColorGrade  *self);

/**
 * lrg_color_grade_set_tint:
 * @self: A #LrgColorGrade
 * @tint: Tint (-1.0 to 1.0, negative = green, positive = magenta)
 *
 * Sets the tint.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_set_tint        (LrgColorGrade  *self,
                                                     gfloat          tint);

/**
 * lrg_color_grade_get_lift:
 * @self: A #LrgColorGrade
 * @r: (out) (optional): Red component
 * @g: (out) (optional): Green component
 * @b: (out) (optional): Blue component
 *
 * Gets the lift (shadows) color adjustment.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_get_lift        (LrgColorGrade  *self,
                                                     gfloat         *r,
                                                     gfloat         *g,
                                                     gfloat         *b);

/**
 * lrg_color_grade_set_lift:
 * @self: A #LrgColorGrade
 * @r: Red (-1.0 to 1.0)
 * @g: Green (-1.0 to 1.0)
 * @b: Blue (-1.0 to 1.0)
 *
 * Sets the lift (shadows) color adjustment.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_set_lift        (LrgColorGrade  *self,
                                                     gfloat          r,
                                                     gfloat          g,
                                                     gfloat          b);

/**
 * lrg_color_grade_get_gamma:
 * @self: A #LrgColorGrade
 * @r: (out) (optional): Red component
 * @g: (out) (optional): Green component
 * @b: (out) (optional): Blue component
 *
 * Gets the gamma (midtones) color adjustment.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_get_gamma       (LrgColorGrade  *self,
                                                     gfloat         *r,
                                                     gfloat         *g,
                                                     gfloat         *b);

/**
 * lrg_color_grade_set_gamma:
 * @self: A #LrgColorGrade
 * @r: Red (0.1 to 4.0, 1.0 = neutral, higher brightens the midtones)
 * @g: Green (0.1 to 4.0)
 * @b: Blue (0.1 to 4.0)
 *
 * Sets the gamma (midtones) color adjustment.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_set_gamma       (LrgColorGrade  *self,
                                                     gfloat          r,
                                                     gfloat          g,
                                                     gfloat          b);

/**
 * lrg_color_grade_get_gain:
 * @self: A #LrgColorGrade
 * @r: (out) (optional): Red component
 * @g: (out) (optional): Green component
 * @b: (out) (optional): Blue component
 *
 * Gets the gain (highlights) color adjustment.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_get_gain        (LrgColorGrade  *self,
                                                     gfloat         *r,
                                                     gfloat         *g,
                                                     gfloat         *b);

/**
 * lrg_color_grade_set_gain:
 * @self: A #LrgColorGrade
 * @r: Red (0.0 to 4.0, 1.0 = neutral)
 * @g: Green (0.0 to 4.0)
 * @b: Blue (0.0 to 4.0)
 *
 * Sets the gain (highlights) color adjustment.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_set_gain        (LrgColorGrade  *self,
                                                     gfloat          r,
                                                     gfloat          g,
                                                     gfloat          b);

/**
 * lrg_color_grade_is_identity:
 * @self: A #LrgColorGrade
 *
 * Whether the grade leaves every colour unchanged: neutral exposure,
 * contrast, saturation, temperature, tint, lift, gamma and gain, or an
 * effect intensity of 0.  A game may skip the grading pass then.
 *
 * Returns: %TRUE when lrg_color_grade_apply_rgb() is the identity
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_color_grade_is_identity     (LrgColorGrade  *self);

/**
 * lrg_color_grade_apply_rgb:
 * @self: A #LrgColorGrade
 * @in: (array fixed-size=3): a colour, each channel 0.0 to 1.0
 * @out: (out caller-allocates) (array fixed-size=3): the graded colour,
 *   clamped to 0.0 to 1.0 (may be @in)
 *
 * The grade's reference on the CPU, the same steps in the same order as
 * the fragment shader (lrg_color_grade_get_fragment_shader()): exposure
 * (times 2^exposure), white balance (temperature scales red up and blue
 * down by up to 20 %, tint scales green down by up to 20 %), lift (added,
 * then clamped at 0), gamma (pow 1 / gamma) and gain (times), contrast
 * around 0.5, saturation against the Rec. 709 luminance, then mixed with
 * the original by the effect's intensity.  Used by tests and by games
 * that grade colours they draw themselves (fog, a clear colour).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_apply_rgb       (LrgColorGrade  *self,
                                                     const gfloat   *in,
                                                     gfloat         *out);

/**
 * lrg_color_grade_get_fragment_shader:
 *
 * The GLSL 330 fragment shader of the grade, for raylib's default
 * vertex shader (fragTexCoord, fragColor, texture0, colDiffuse)
 * with the uniforms exposure, temperature, tint, lift, gamma,
 * gain, contrast, saturation and intensity.
 *
 * Returns: (transfer none): the shader source
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_color_grade_get_fragment_shader (void);

/**
 * lrg_color_grade_get_shader:
 * @self: A #LrgColorGrade
 *
 * The compiled grading shader, built on first use.  Needs a window (an
 * OpenGL context); without one, or when the shader does not compile,
 * %NULL (and the effect draws its source unchanged).
 *
 * Returns: (transfer none) (nullable): the shader
 */
LRG_AVAILABLE_IN_ALL
GrlShader *         lrg_color_grade_get_shader      (LrgColorGrade  *self);

/**
 * lrg_color_grade_draw_texture:
 * @self: A #LrgColorGrade
 * @texture: the texture to draw (a render texture's colour attachment)
 * @source: the part of @texture, in texels (a negative height flips it,
 *   as render textures are stored bottom up)
 * @dest: where to draw it, in screen pixels
 *
 * Draws @texture graded into the current target (the screen or a render
 * texture).  An identity grade draws it plainly; without a shader it is
 * drawn plainly too and %FALSE is returned.
 *
 * Returns: %TRUE when the grade was applied (or was the identity)
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_color_grade_draw_texture    (LrgColorGrade      *self,
                                                     GrlTexture         *texture,
                                                     const GrlRectangle *source,
                                                     const GrlRectangle *dest);

/**
 * lrg_color_grade_set_lut:
 * @self: A #LrgColorGrade
 * @lut: (nullable): a lookup table applied after the grade's own steps,
 *   or %NULL for none
 * @strength: 0 (no table) .. 1 (the table's colours)
 *
 * Adds a 3D lookup table to the grade: the shader (and
 * lrg_color_grade_apply_rgb()) looks the graded, clamped colour up in
 * @lut and mixes it in by @strength, before the effect's intensity. The
 * table is uploaded as a strip texture when it changes; a grade with a
 * table is never the identity.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_set_lut         (LrgColorGrade  *self,
                                                     LrgColorLut    *lut,
                                                     gfloat          strength);

/**
 * lrg_color_grade_get_lut:
 * @self: A #LrgColorGrade
 * @out_strength: (out) (optional): the table's strength (0 without one)
 *
 * Returns: (transfer none) (nullable): the lookup table
 */
LRG_AVAILABLE_IN_ALL
LrgColorLut *       lrg_color_grade_get_lut         (LrgColorGrade  *self,
                                                     gfloat         *out_strength);

/**
 * lrg_color_grade_bake_lut:
 * @self: A #LrgColorGrade
 * @size: the table's side
 *
 * The whole grade (its steps, its table and its intensity) sampled into
 * a new table, e.g. to blend two grades or to save one as a .cube file.
 *
 * Returns: (transfer full): the table
 */
LRG_AVAILABLE_IN_ALL
LrgColorLut *       lrg_color_grade_bake_lut        (LrgColorGrade  *self,
                                                     guint           size);

G_END_DECLS
