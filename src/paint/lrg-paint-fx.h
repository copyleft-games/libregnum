/* lrg-paint-fx.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Effects and materials: backdrop blur, glass and effect shaders.
 *
 * These are the parameter blocks for the paint list's blur, glass,
 * effect and curve primitives, plus the registry of named effects
 * (built-in and user GLSL snippets).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-paint-types.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgPaintGlass
 * ========================================================================== */

#define LRG_TYPE_PAINT_GLASS (lrg_paint_glass_get_type ())

/**
 * LrgPaintGlass:
 * @frost: 0 shows the backdrop sharp, 1 fully blurred
 * @refraction: how far the rim bends the backdrop, in canvas units
 *   (0 disables refraction)
 * @saturation: colour saturation of what shows through: 1 keeps it,
 *   above 1 makes it more vivid, 0 greys it out
 * @tint: colour laid over the glass; its alpha is the tint strength
 * @rim_light: strength of the specular rim light from the top-left,
 *   0..1
 * @dispersion: chromatic split at the rim, 0..1 (0 disables it)
 * @blur_radius: blur radius of the frosted backdrop, in canvas units
 *
 * Look of a glass slab drawn with lrg_paint_list_glass(). A plain
 * value struct; lrg_paint_glass_init_default() gives a clear glass.
 */
typedef struct
{
	gfloat   frost;
	gfloat   refraction;
	gfloat   saturation;
	GrlColor tint;
	gfloat   rim_light;
	gfloat   dispersion;
	gfloat   blur_radius;
} LrgPaintGlass;

LRG_AVAILABLE_IN_ALL
GType lrg_paint_glass_get_type (void) G_GNUC_CONST;

/**
 * lrg_paint_glass_copy:
 * @self: an #LrgPaintGlass
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgPaintGlass * lrg_paint_glass_copy (const LrgPaintGlass *self);

/**
 * lrg_paint_glass_free:
 * @self: (nullable): an #LrgPaintGlass
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_glass_free (LrgPaintGlass *self);

/**
 * lrg_paint_glass_init_default:
 * @self: (out caller-allocates): glass to set
 *
 * Clear glass: frost 0.35, refraction 8, saturation 1.2, a faint white
 * tint, rim light 0.6, dispersion 0.25 and a 12 unit blur.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_glass_init_default (LrgPaintGlass *self);

/**
 * lrg_paint_glass_lerp:
 * @a: glass at t = 0
 * @b: glass at t = 1
 * @t: blend factor
 * @out: (out caller-allocates): the blended glass
 *
 * Blends every parameter of two glass looks.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_glass_lerp (const LrgPaintGlass *a,
                           const LrgPaintGlass *b,
                           gfloat               t,
                           LrgPaintGlass       *out);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPaintGlass, lrg_paint_glass_free)

/* ==========================================================================
 * LrgPaintEffectParams
 * ========================================================================== */

#define LRG_TYPE_PAINT_EFFECT_PARAMS (lrg_paint_effect_params_get_type ())

/**
 * LrgPaintEffectParams:
 * @color1: first palette colour (usually the base)
 * @color2: second palette colour
 * @color3: third palette colour (highlights)
 * @intensity: strength of the effect, 0..1 (some effects allow more)
 * @hover: how much the pointer is over the shape, 0..1 (a widget's
 *   hover spring)
 * @press: how much the shape is pressed, 0..1
 * @speed: animation speed multiplier, 1 is the designed pace
 * @scale: pattern scale multiplier, 1 is the designed size
 *
 * Inputs of an effect drawn with lrg_paint_list_effect(). Time and the
 * pointer position come from the list (lrg_paint_list_set_time(),
 * lrg_paint_list_set_pointer()).
 */
typedef struct
{
	GrlColor color1;
	GrlColor color2;
	GrlColor color3;
	gfloat   intensity;
	gfloat   hover;
	gfloat   press;
	gfloat   speed;
	gfloat   scale;
} LrgPaintEffectParams;

LRG_AVAILABLE_IN_ALL
GType lrg_paint_effect_params_get_type (void) G_GNUC_CONST;

/**
 * lrg_paint_effect_params_copy:
 * @self: an #LrgPaintEffectParams
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgPaintEffectParams * lrg_paint_effect_params_copy (const LrgPaintEffectParams *self);

/**
 * lrg_paint_effect_params_free:
 * @self: (nullable): an #LrgPaintEffectParams
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_effect_params_free (LrgPaintEffectParams *self);

/**
 * lrg_paint_effect_params_init_default:
 * @self: (out caller-allocates): params to set
 *
 * A dark blue base, violet and cyan accents, intensity 1, no hover or
 * press, speed and scale 1.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_effect_params_init_default (LrgPaintEffectParams *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPaintEffectParams, lrg_paint_effect_params_free)

/* ==========================================================================
 * LrgPaintCurveStyle
 * ========================================================================== */

#define LRG_TYPE_PAINT_CURVE_STYLE (lrg_paint_curve_style_get_type ())

/**
 * LrgPaintCurveStyle:
 * @color_start: stroke colour at the start of the curve
 * @color_end: stroke colour at the end (the stroke fades between them)
 * @glow: colour of the halo around the stroke (alpha 0 disables it)
 * @glow_size: halo width beyond the stroke edge, in canvas units
 * @pulse_color: colour of the travelling pulses
 * @pulse_count: number of pulses on the curve (0 disables them, at
 *   most 8)
 * @pulse_speed: pulse speed in curve lengths per second (negative runs
 *   from the end to the start)
 * @pulse_phase: offset of the pulses along the curve, 0..1
 *
 * Look of a wire drawn with lrg_paint_list_curve(). Pulses move with
 * the list time (lrg_paint_list_set_time()).
 */
typedef struct
{
	GrlColor color_start;
	GrlColor color_end;
	GrlColor glow;
	gfloat   glow_size;
	GrlColor pulse_color;
	guint    pulse_count;
	gfloat   pulse_speed;
	gfloat   pulse_phase;
} LrgPaintCurveStyle;

LRG_AVAILABLE_IN_ALL
GType lrg_paint_curve_style_get_type (void) G_GNUC_CONST;

/**
 * lrg_paint_curve_style_copy:
 * @self: an #LrgPaintCurveStyle
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgPaintCurveStyle * lrg_paint_curve_style_copy (const LrgPaintCurveStyle *self);

/**
 * lrg_paint_curve_style_free:
 * @self: (nullable): an #LrgPaintCurveStyle
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_curve_style_free (LrgPaintCurveStyle *self);

/**
 * lrg_paint_curve_style_init:
 * @self: (out caller-allocates): style to set
 * @color: stroke colour (both ends)
 *
 * A plain stroke of one colour: no glow, no pulses.
 */
LRG_AVAILABLE_IN_ALL
void lrg_paint_curve_style_init (LrgPaintCurveStyle *self,
                                 const GrlColor     *color);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPaintCurveStyle, lrg_paint_curve_style_free)

/* ==========================================================================
 * Effect registry
 * ========================================================================== */

/**
 * LRG_PAINT_EFFECT_NONE:
 *
 * Effect id meaning "no effect".
 */
#define LRG_PAINT_EFFECT_NONE (0)

/**
 * LRG_PAINT_EFFECT_FIRST_USER:
 *
 * Id of the first effect registered with lrg_paint_effect_register();
 * built-in effects use the ids below it.
 */
#define LRG_PAINT_EFFECT_FIRST_USER (64)

/**
 * LRG_PAINT_EFFECT_MAX_USER:
 *
 * Maximum number of user effects that can be registered.
 */
#define LRG_PAINT_EFFECT_MAX_USER (32)

/**
 * lrg_paint_effect_lookup:
 * @name: an effect name: "shimmer", "aurora", "neon", "spotlight",
 *   "ripple", "grain", "plasma", "mesh-gradient", or a name given to
 *   lrg_paint_effect_register()
 *
 * Returns: the effect id, or -1 when no effect has that name
 */
LRG_AVAILABLE_IN_ALL
gint lrg_paint_effect_lookup (const gchar *name);

/**
 * lrg_paint_effect_get_name:
 * @effect_id: an effect id
 *
 * Returns: (nullable): the effect's name, or %NULL for an unknown id
 *   or %LRG_PAINT_EFFECT_NONE
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_paint_effect_get_name (gint effect_id);

/**
 * lrg_paint_effect_list_names:
 *
 * Returns: (transfer full) (array zero-terminated=1): the names of
 *   every effect, built-in ones first
 */
LRG_AVAILABLE_IN_ALL
gchar ** lrg_paint_effect_list_names (void);

/**
 * lrg_paint_effect_register:
 * @name: effect name: lower-case letters, digits and '-'; must not be
 *   taken
 * @glsl_snippet: GLSL 330 source that defines
 *   `vec4 lrg_effect (LrgEffect fx)` (see the paint module docs for
 *   the full contract)
 * @error: return location for a #GError
 *
 * Adds a user effect. The snippet is compiled into a variant of the
 * paint shader the next time a renderer draws, so user effects batch
 * with everything else. When the variant fails to compile the
 * renderer logs the GLSL error, keeps using the built-in shader and
 * draws user effects as their first colour.
 *
 * Errors are %G_IO_ERROR_INVALID_ARGUMENT for a bad name or snippet,
 * %G_IO_ERROR_EXISTS for a name in use and %G_IO_ERROR_NO_SPACE when
 * %LRG_PAINT_EFFECT_MAX_USER effects are registered.
 *
 * Returns: the new effect id, or -1 on error
 */
LRG_AVAILABLE_IN_ALL
gint lrg_paint_effect_register (const gchar  *name,
                                const gchar  *glsl_snippet,
                                GError      **error);

G_END_DECLS
