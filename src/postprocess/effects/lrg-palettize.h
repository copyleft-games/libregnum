/* lrg-palettize.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Palettize post-processing effect: every pixel to the nearest colour of
 * a fixed palette (up to 256 colours), with an optional ordered dither.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-post-effect.h"

G_BEGIN_DECLS

/**
 * LRG_PALETTIZE_MAX_COLORS:
 *
 * The most colours a palette may hold.
 */
#define LRG_PALETTIZE_MAX_COLORS (256)

/**
 * LRG_PALETTIZE_LUT_SIZE:
 *
 * Levels per channel of the nearest-colour lookup table the shader
 * samples (32: a 32 x 32 x 32 cube).
 */
#define LRG_PALETTIZE_LUT_SIZE (32)

/**
 * LRG_PALETTIZE_BAYER_SIZE:
 *
 * The side of the ordered-dither matrix (8: 64 thresholds).
 */
#define LRG_PALETTIZE_BAYER_SIZE (8)

#define LRG_TYPE_PALETTIZE (lrg_palettize_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPalettize, lrg_palettize, LRG, PALETTIZE, LrgPostEffect)

/**
 * lrg_palettize_new:
 *
 * Creates a palettize effect without a palette (the identity until
 * lrg_palettize_set_palette()).
 *
 * Returns: (transfer full): A new #LrgPalettize
 */
LRG_AVAILABLE_IN_ALL
LrgPalettize *      lrg_palettize_new               (void);

/**
 * lrg_palettize_set_palette:
 * @self: A #LrgPalettize
 * @rgb: packed 8-bit red, green, blue triples, 1 to 256 colours
 * @error: return location for a #GError
 *
 * Sets the palette and rebuilds the nearest-colour table.  A size that is
 * not a whole number of triples, or holds no colour or more than
 * %LRG_PALETTIZE_MAX_COLORS, fails with %G_IO_ERROR_INVALID_DATA and
 * leaves the palette unchanged.
 *
 * Returns: %TRUE when the palette was set
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_palettize_set_palette       (LrgPalettize  *self,
                                                     GBytes        *rgb,
                                                     GError       **error);

/**
 * lrg_palettize_clear_palette:
 * @self: A #LrgPalettize
 *
 * Drops the palette: the effect is the identity again.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_palettize_clear_palette     (LrgPalettize  *self);

/**
 * lrg_palettize_get_palette_size:
 * @self: A #LrgPalettize
 *
 * Returns: the number of colours, 0 without a palette
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_palettize_get_palette_size  (LrgPalettize  *self);

/**
 * lrg_palettize_get_color:
 * @self: A #LrgPalettize
 * @index: a palette index
 * @r: (out) (optional): red
 * @g: (out) (optional): green
 * @b: (out) (optional): blue
 *
 * Returns: %TRUE when @index is in the palette
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_palettize_get_color         (LrgPalettize  *self,
                                                     guint          index,
                                                     guint8        *r,
                                                     guint8        *g,
                                                     guint8        *b);

/**
 * lrg_palettize_find_nearest:
 * @self: A #LrgPalettize with a palette
 * @r: red
 * @g: green
 * @b: blue
 *
 * The exact nearest palette colour: the smallest squared distance in
 * 8-bit RGB, the lowest index on a tie.
 *
 * Returns: the palette index, 0 without a palette
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_palettize_find_nearest      (LrgPalettize  *self,
                                                     guint8         r,
                                                     guint8         g,
                                                     guint8         b);

/**
 * lrg_palettize_get_lut_index:
 * @self: A #LrgPalettize with a palette
 * @r: red level, 0 to %LRG_PALETTIZE_LUT_SIZE - 1
 * @g: green level
 * @b: blue level
 *
 * The lookup table's entry the shader samples: the nearest palette colour
 * (lrg_palettize_find_nearest()) to the cell's colour, level * 255 / 31
 * rounded.
 *
 * Returns: the palette index, 0 without a palette or out of range
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_palettize_get_lut_index     (LrgPalettize  *self,
                                                     guint          r,
                                                     guint          g,
                                                     guint          b);

/**
 * lrg_palettize_get_bayer:
 * @x: a pixel column
 * @y: a pixel row
 *
 * The 8 x 8 ordered-dither (Bayer) threshold of a pixel, repeating every
 * eight pixels: each of 0 .. 63 once per tile.
 *
 * Returns: the threshold, 0 to 63
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_palettize_get_bayer         (guint          x,
                                                     guint          y);

/**
 * lrg_palettize_get_dither:
 * @self: A #LrgPalettize
 *
 * Returns: whether the ordered dither runs before the palette lookup
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_palettize_get_dither        (LrgPalettize  *self);

/**
 * lrg_palettize_set_dither:
 * @self: A #LrgPalettize
 * @dither: whether to dither
 *
 * Sets the ordered dither on or off.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_palettize_set_dither        (LrgPalettize  *self,
                                                     gboolean       dither);

/**
 * lrg_palettize_get_dither_strength:
 * @self: A #LrgPalettize
 *
 * Returns: the dither's spread, 0 to 1 of the channel range
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_palettize_get_dither_strength (LrgPalettize *self);

/**
 * lrg_palettize_set_dither_strength:
 * @self: A #LrgPalettize
 * @strength: the spread, 0 to 1 (clamped): each channel moves by up to
 *   half of it, up or down, by the pixel's threshold
 *
 * Sets the dither's spread (default 0.1).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_palettize_set_dither_strength (LrgPalettize *self,
                                                       gfloat        strength);

/**
 * lrg_palettize_get_dither_scale:
 * @self: A #LrgPalettize
 *
 * Returns: target pixels per dither cell
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_palettize_get_dither_scale  (LrgPalettize  *self);

/**
 * lrg_palettize_set_dither_scale:
 * @self: A #LrgPalettize
 * @scale: target pixels per dither cell, at least 1 (clamped); a game
 *   drawing a low-resolution view straight to a large screen gives the
 *   view's pixel size so the pattern follows its pixels
 *
 * Sets the dither cell's size (default 1).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_palettize_set_dither_scale  (LrgPalettize  *self,
                                                     gfloat         scale);

/**
 * lrg_palettize_is_identity:
 * @self: A #LrgPalettize
 *
 * Returns: %TRUE without a palette or with an effect intensity of 0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_palettize_is_identity       (LrgPalettize  *self);

/**
 * lrg_palettize_apply_rgb:
 * @self: A #LrgPalettize
 * @x: the pixel's column in dither cells
 * @y: the pixel's row in dither cells
 * @in: (array fixed-size=3): a colour, each channel 0.0 to 1.0
 * @out: (out caller-allocates) (array fixed-size=3): the palette colour
 *   (may be @in)
 *
 * The effect's reference on the CPU, the fragment shader's steps: with
 * the dither on, each channel moves by ((threshold + 0.5) / 64 - 0.5)
 * times the strength; the colour is clamped, rounded to the table's 32
 * levels, and the table's palette colour is mixed with the original by
 * the effect's intensity.  Without a palette @out is @in.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_palettize_apply_rgb         (LrgPalettize  *self,
                                                     guint          x,
                                                     guint          y,
                                                     const gfloat  *in,
                                                     gfloat        *out);

/**
 * lrg_palettize_get_fragment_shader:
 *
 * The GLSL 330 fragment shader, for raylib's default vertex shader, with
 * the uniforms lut (the table as a 1024 x 32 texture: column r + 32 b,
 * row g), dither, ditherStrength, ditherScale and intensity.
 *
 * Returns: (transfer none): the shader source
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_palettize_get_fragment_shader (void);

/**
 * lrg_palettize_draw_texture:
 * @self: A #LrgPalettize
 * @texture: the texture to draw (a render texture's colour attachment)
 * @source: the part of @texture, in texels (a negative height flips it)
 * @dest: where to draw it, in pixels of the current target
 *
 * Draws @texture palettized into the current target.  The shader and the
 * table's texture are made on first use (they need a window).  An
 * identity effect draws plainly; without a shader it is drawn plainly
 * and %FALSE is returned.
 *
 * Returns: %TRUE when the effect was applied (or was the identity)
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_palettize_draw_texture      (LrgPalettize       *self,
                                                     GrlTexture         *texture,
                                                     const GrlRectangle *source,
                                                     const GrlRectangle *dest);

G_END_DECLS
