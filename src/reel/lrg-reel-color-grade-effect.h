/* lrg-reel-color-grade-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelColorGradeEffect - per-pixel brightness/contrast/saturation grade.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-reel-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_COLOR_GRADE_EFFECT (lrg_reel_color_grade_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelColorGradeEffect, lrg_reel_color_grade_effect,
                      LRG, REEL_COLOR_GRADE_EFFECT, LrgReelEffect)

/**
 * lrg_reel_color_grade_effect_new:
 *
 * Creates a new #LrgReelColorGradeEffect with identity settings
 * (brightness=0, contrast=1, saturation=1).
 *
 * Returns: (transfer full): a new #LrgReelColorGradeEffect
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelColorGradeEffect *
lrg_reel_color_grade_effect_new (void);

/**
 * lrg_reel_color_grade_effect_get_brightness:
 * @self: a #LrgReelColorGradeEffect
 *
 * Returns: the brightness offset in the range [-1, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_color_grade_effect_get_brightness (LrgReelColorGradeEffect *self);

/**
 * lrg_reel_color_grade_effect_set_brightness:
 * @self: a #LrgReelColorGradeEffect
 * @brightness: additive brightness offset [-1, 1]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_color_grade_effect_set_brightness (LrgReelColorGradeEffect *self,
                                             gdouble                  brightness);

/**
 * lrg_reel_color_grade_effect_get_contrast:
 * @self: a #LrgReelColorGradeEffect
 *
 * Returns: the contrast multiplier in the range [0.5, 2]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_color_grade_effect_get_contrast (LrgReelColorGradeEffect *self);

/**
 * lrg_reel_color_grade_effect_set_contrast:
 * @self: a #LrgReelColorGradeEffect
 * @contrast: contrast multiplier around 0.5 [0.5, 2]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_color_grade_effect_set_contrast (LrgReelColorGradeEffect *self,
                                           gdouble                  contrast);

/**
 * lrg_reel_color_grade_effect_get_saturation:
 * @self: a #LrgReelColorGradeEffect
 *
 * Returns: the saturation multiplier [0, 2] (1 = no change)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_color_grade_effect_get_saturation (LrgReelColorGradeEffect *self);

/**
 * lrg_reel_color_grade_effect_set_saturation:
 * @self: a #LrgReelColorGradeEffect
 * @saturation: saturation multiplier [0, 2]
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_color_grade_effect_set_saturation (LrgReelColorGradeEffect *self,
                                             gdouble                  saturation);

G_END_DECLS
