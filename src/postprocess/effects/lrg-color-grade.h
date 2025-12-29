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
#include "../lrg-post-effect.h"

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
 * @r: Red (-1.0 to 1.0)
 * @g: Green (-1.0 to 1.0)
 * @b: Blue (-1.0 to 1.0)
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
 * @r: Red (-1.0 to 1.0)
 * @g: Green (-1.0 to 1.0)
 * @b: Blue (-1.0 to 1.0)
 *
 * Sets the gain (highlights) color adjustment.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_color_grade_set_gain        (LrgColorGrade  *self,
                                                     gfloat          r,
                                                     gfloat          g,
                                                     gfloat          b);

G_END_DECLS
