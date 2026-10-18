/* lrg-color-lut.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColorLut - a 3D colour lookup table: N x N x N output colours over
 * the unit cube of input colours, sampled trilinearly. Reads and writes
 * the .cube format (Adobe / Resolve: LUT_3D_SIZE, LUT_1D_SIZE, DOMAIN_MIN,
 * DOMAIN_MAX, LUT_3D_INPUT_RANGE, TITLE; red changes fastest), bakes an
 * #LrgColorGrade into a table and blends two tables. Immutable and
 * reference counted; #LrgColorGrade applies one on the GPU
 * (lrg_color_grade_set_lut()). Headless.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLOR_LUT (lrg_color_lut_get_type ())

/**
 * LRG_COLOR_LUT_MIN_SIZE:
 *
 * The smallest table side.
 */
#define LRG_COLOR_LUT_MIN_SIZE (2)

/**
 * LRG_COLOR_LUT_MAX_SIZE:
 *
 * The largest table side (64^3 entries).
 */
#define LRG_COLOR_LUT_MAX_SIZE (64)

/**
 * LRG_COLOR_LUT_ERROR:
 *
 * Error domain of the .cube reader.
 */
#define LRG_COLOR_LUT_ERROR (lrg_color_lut_error_quark ())

/**
 * LrgColorLutError:
 * @LRG_COLOR_LUT_ERROR_PARSE: a line is neither a keyword nor three numbers
 * @LRG_COLOR_LUT_ERROR_SIZE: no size, a size out of range, or a number of
 *   entries that does not match it
 * @LRG_COLOR_LUT_ERROR_DOMAIN: a domain whose maximum is not above its
 *   minimum
 * @LRG_COLOR_LUT_ERROR_IO: the file cannot be read
 *
 * What went wrong reading a table.
 */
typedef enum
{
    LRG_COLOR_LUT_ERROR_PARSE,
    LRG_COLOR_LUT_ERROR_SIZE,
    LRG_COLOR_LUT_ERROR_DOMAIN,
    LRG_COLOR_LUT_ERROR_IO
} LrgColorLutError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_color_lut_error_quark (void);

/**
 * LrgColorLut:
 *
 * An immutable 3D colour table (see the file description).
 */
typedef struct _LrgColorLut LrgColorLut;

LRG_AVAILABLE_IN_ALL
GType lrg_color_lut_get_type (void) G_GNUC_CONST;

/**
 * lrg_color_lut_new_identity:
 * @size: the side, %LRG_COLOR_LUT_MIN_SIZE .. %LRG_COLOR_LUT_MAX_SIZE
 *
 * Returns: (transfer full): a table giving every colour back unchanged
 */
LRG_AVAILABLE_IN_ALL
LrgColorLut *
lrg_color_lut_new_identity (guint size);

/**
 * lrg_color_lut_new_from_data:
 * @size: the side, %LRG_COLOR_LUT_MIN_SIZE .. %LRG_COLOR_LUT_MAX_SIZE
 * @rgb: (array): @size^3 x 3 output values, red index fastest, then
 *   green, then blue (the .cube order)
 * @title: (nullable): a name
 *
 * Returns: (transfer full): a new table (values are copied; non-finite
 *   ones become 0)
 */
LRG_AVAILABLE_IN_ALL
LrgColorLut *
lrg_color_lut_new_from_data (guint         size,
                             const gfloat *rgb,
                             const gchar  *title);

/**
 * lrg_color_lut_new_from_cube:
 * @text: the contents of a .cube file
 * @length: the length of @text, or -1 when it ends with a NUL
 * @error: return location for a #GError in %LRG_COLOR_LUT_ERROR
 *
 * Parses a .cube table. A 3D table (LUT_3D_SIZE 2 .. 64) is taken as
 * is; a 1D table (LUT_1D_SIZE 2 .. 65536, one curve per channel) becomes
 * a 33-sided 3D table. A domain other than 0 .. 1 (DOMAIN_MIN /
 * DOMAIN_MAX or LUT_3D_INPUT_RANGE) is resampled into 0 .. 1. Comments
 * (#), blank lines and other keywords are skipped.
 *
 * Returns: (transfer full) (nullable): the table, or %NULL with @error
 */
LRG_AVAILABLE_IN_ALL
LrgColorLut *
lrg_color_lut_new_from_cube (const gchar  *text,
                             gssize        length,
                             GError      **error);

/**
 * lrg_color_lut_new_from_file:
 * @path: a .cube file
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the table, or %NULL with @error
 *   (%LRG_COLOR_LUT_ERROR_IO when the file cannot be read)
 */
LRG_AVAILABLE_IN_ALL
LrgColorLut *
lrg_color_lut_new_from_file (const gchar  *path,
                             GError      **error);

/**
 * lrg_color_lut_new_mix:
 * @a: a table
 * @b: another table
 * @t: 0 (@a) .. 1 (@b)
 *
 * The entrywise blend of two tables at @a's size (@b is sampled at @a's
 * grid points when the sizes differ).
 *
 * Returns: (transfer full): the blend
 */
LRG_AVAILABLE_IN_ALL
LrgColorLut *
lrg_color_lut_new_mix (LrgColorLut *a,
                       LrgColorLut *b,
                       gfloat       t);

/**
 * lrg_color_lut_new_resampled:
 * @self: a table
 * @size: the new side
 *
 * Returns: (transfer full): @self sampled at a @size grid
 */
LRG_AVAILABLE_IN_ALL
LrgColorLut *
lrg_color_lut_new_resampled (LrgColorLut *self,
                             guint        size);

/**
 * lrg_color_lut_ref:
 * @self: a table
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgColorLut *
lrg_color_lut_ref (LrgColorLut *self);

/**
 * lrg_color_lut_unref:
 * @self: (transfer full): a table
 */
LRG_AVAILABLE_IN_ALL
void
lrg_color_lut_unref (LrgColorLut *self);

/**
 * lrg_color_lut_get_size:
 * @self: a table
 *
 * Returns: the side
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_color_lut_get_size (LrgColorLut *self);

/**
 * lrg_color_lut_get_title:
 * @self: a table
 *
 * Returns: (transfer none) (nullable): the TITLE, if any
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_color_lut_get_title (LrgColorLut *self);

/**
 * lrg_color_lut_get_entry:
 * @self: a table
 * @r: red index, 0 .. size - 1
 * @g: green index
 * @b: blue index
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the entry
 *
 * Returns: %FALSE for an index out of range
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_color_lut_get_entry (LrgColorLut *self,
                         guint        r,
                         guint        g,
                         guint        b,
                         gfloat      *out_rgb);

/**
 * lrg_color_lut_sample:
 * @self: a table
 * @rgb: (array fixed-size=3): an input colour (clamped to 0 .. 1)
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the trilinear
 *   lookup (may be @rgb)
 *
 * The CPU reference of the GPU lookup (lrg_color_grade_set_lut()).
 */
LRG_AVAILABLE_IN_ALL
void
lrg_color_lut_sample (LrgColorLut  *self,
                      const gfloat *rgb,
                      gfloat       *out_rgb);

/**
 * lrg_color_lut_apply_rgb:
 * @self: a table
 * @rgb: (array fixed-size=3): an input colour
 * @strength: 0 (unchanged) .. 1 (the table's colour)
 * @out_rgb: (out caller-allocates) (array fixed-size=3): the result
 *   (may be @rgb)
 */
LRG_AVAILABLE_IN_ALL
void
lrg_color_lut_apply_rgb (LrgColorLut  *self,
                         const gfloat *rgb,
                         gfloat        strength,
                         gfloat       *out_rgb);

/**
 * lrg_color_lut_is_identity:
 * @self: a table
 * @tolerance: largest difference allowed per channel
 *
 * Returns: %TRUE when every entry is within @tolerance of its grid point
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_color_lut_is_identity (LrgColorLut *self,
                           gfloat       tolerance);

/**
 * lrg_color_lut_to_cube:
 * @self: a table
 *
 * Writes @self as a .cube file (TITLE when it has one, LUT_3D_SIZE, the
 * entries with six decimals) that lrg_color_lut_new_from_cube() reads
 * back.
 *
 * Returns: (transfer full): the text
 */
LRG_AVAILABLE_IN_ALL
gchar *
lrg_color_lut_to_cube (LrgColorLut *self);

/**
 * lrg_color_lut_to_strip:
 * @self: a table
 * @out_width: (out): size x size
 * @out_height: (out): size
 *
 * The table as an RGBA8 image strip for the GPU: blue slices side by
 * side, red across each slice, green down the rows (row 0 first).
 *
 * Returns: (transfer full): the pixels, @out_width x @out_height x 4 bytes
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_color_lut_to_strip (LrgColorLut *self,
                        guint       *out_width,
                        guint       *out_height);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgColorLut, lrg_color_lut_unref)

G_END_DECLS
