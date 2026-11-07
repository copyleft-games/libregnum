/* lrg-tonemap.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTonemap: exposure and an HDR -> display tonemapping curve
 * (Reinhard, Hable filmic or ACES).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../lrg-post-effect.h"

G_BEGIN_DECLS

/**
 * LrgTonemapOperator:
 * @LRG_TONEMAP_OPERATOR_NONE: Exposure only, then clamp to 0..1
 * @LRG_TONEMAP_OPERATOR_REINHARD: Extended Reinhard, x(1 + x/W^2)/(1 + x):
 *   soft and desaturating; reaches white at the white point W
 * @LRG_TONEMAP_OPERATOR_FILMIC: John Hable's "Uncharted 2" filmic curve,
 *   normalised so the white point W maps to 1; a toe and a long shoulder
 * @LRG_TONEMAP_OPERATOR_ACES: Krzysztof Narkowicz's fit of the ACES
 *   reference rendering transform; contrasty and saturated
 *
 * The curve #LrgTonemap maps scene values with.
 */
typedef enum
{
	LRG_TONEMAP_OPERATOR_NONE,
	LRG_TONEMAP_OPERATOR_REINHARD,
	LRG_TONEMAP_OPERATOR_FILMIC,
	LRG_TONEMAP_OPERATOR_ACES
} LrgTonemapOperator;

/**
 * lrg_tonemap_operator_get_type:
 *
 * Gets the #GType of #LrgTonemapOperator.
 *
 * Returns: the enum #GType
 */
LRG_AVAILABLE_IN_ALL
GType lrg_tonemap_operator_get_type (void) G_GNUC_CONST;

#define LRG_TYPE_TONEMAP_OPERATOR (lrg_tonemap_operator_get_type ())

#define LRG_TYPE_TONEMAP (lrg_tonemap_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTonemap, lrg_tonemap, LRG, TONEMAP, LrgPostEffect)

/**
 * lrg_tonemap_new:
 *
 * Creates a tonemap effect: ACES, exposure 0 EV, white point 4, sRGB
 * (gamma 2.2) input. Its name is "tonemap". Put it before effects that
 * expect display values (colour grade, vignette, FXAA, film grain) and
 * after bloom, which wants the HDR values.
 *
 * Returns: (transfer full): a new #LrgTonemap
 */
LRG_AVAILABLE_IN_ALL
LrgTonemap *        lrg_tonemap_new                 (void);

/**
 * lrg_tonemap_get_operator:
 * @self: A #LrgTonemap
 *
 * Gets the tonemapping curve.
 *
 * Returns: The operator
 */
LRG_AVAILABLE_IN_ALL
LrgTonemapOperator  lrg_tonemap_get_operator        (LrgTonemap         *self);

/**
 * lrg_tonemap_set_operator:
 * @self: A #LrgTonemap
 * @op: The operator
 *
 * Sets the tonemapping curve.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tonemap_set_operator        (LrgTonemap         *self,
                                                     LrgTonemapOperator  op);

/**
 * lrg_tonemap_get_exposure:
 * @self: A #LrgTonemap
 *
 * Gets the exposure in stops (EV).
 *
 * Returns: The exposure
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_tonemap_get_exposure        (LrgTonemap         *self);

/**
 * lrg_tonemap_set_exposure:
 * @self: A #LrgTonemap
 * @ev: Exposure in stops, -16..16; linear values are multiplied by 2^@ev
 *   before the curve
 *
 * Sets the exposure.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tonemap_set_exposure        (LrgTonemap         *self,
                                                     gfloat              ev);

/**
 * lrg_tonemap_get_white_point:
 * @self: A #LrgTonemap
 *
 * Gets the white point.
 *
 * Returns: The white point
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_tonemap_get_white_point     (LrgTonemap         *self);

/**
 * lrg_tonemap_set_white_point:
 * @self: A #LrgTonemap
 * @white: The linear scene value that maps to display white, 0.1..100
 *   (Reinhard and filmic; ACES and none ignore it)
 *
 * Sets the white point.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tonemap_set_white_point     (LrgTonemap         *self,
                                                     gfloat              white);

/**
 * lrg_tonemap_get_linear_input:
 * @self: A #LrgTonemap
 *
 * Gets whether the input is treated as linear light.
 *
 * Returns: %TRUE if the input is linear
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tonemap_get_linear_input    (LrgTonemap         *self);

/**
 * lrg_tonemap_set_linear_input:
 * @self: A #LrgTonemap
 * @linear: Whether the image holds linear light
 *
 * %FALSE (the default) decodes the input with gamma 2.2 before the curve,
 * which is right for the engine's shaders (they write gamma-encoded
 * colour, values above 1.0 kept in the HDR target). %TRUE skips the
 * decode, for linear renderers. The output is always gamma 2.2 encoded.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tonemap_set_linear_input    (LrgTonemap         *self,
                                                     gboolean            linear);

/**
 * lrg_tonemap_apply_rgb:
 * @self: A #LrgTonemap
 * @rgb: (array fixed-size=3): Input colour
 * @out: (array fixed-size=3) (out caller-allocates): Output colour, 0..1
 *
 * The shader's arithmetic on the CPU (decode, exposure, curve, encode,
 * mixed with the input by the base intensity). @rgb and @out may alias.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tonemap_apply_rgb           (LrgTonemap         *self,
                                                     const gfloat       *rgb,
                                                     gfloat             *out);

/**
 * lrg_tonemap_get_fragment_shader:
 *
 * Gets the GLSL 330 fragment shader the effect draws with.
 *
 * Returns: (transfer none): The shader source
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_tonemap_get_fragment_shader (void);

G_END_DECLS
