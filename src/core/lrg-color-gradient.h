/* lrg-color-gradient.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColorGradient: the engine's runtime colour gradient.  A sorted list
 * of colour stops (offset 0..1, RGBA floats 0..1) with an interpolation
 * mode, exact and table-driven evaluation, a one-line text form and
 * conversion to and from the editor's #LrgGradientStops.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../ui/lrg-gradient-edit.h"

G_BEGIN_DECLS

/**
 * LRG_COLOR_GRADIENT_LUT_SIZE:
 *
 * Number of entries in the table lrg_color_gradient_evaluate_fast()
 * samples (a table entry every 1/255 of the range).
 */
#define LRG_COLOR_GRADIENT_LUT_SIZE (256)

/**
 * LrgColorGradientMode:
 * @LRG_COLOR_GRADIENT_LINEAR: linear blend between neighbouring stops
 * @LRG_COLOR_GRADIENT_STEP: hold each stop's colour up to the next stop
 * @LRG_COLOR_GRADIENT_SMOOTH: smoothstep (3t^2 - 2t^3) blend between
 *   neighbouring stops
 *
 * How a #LrgColorGradient fills the space between two stops.
 */
typedef enum
{
	LRG_COLOR_GRADIENT_LINEAR,
	LRG_COLOR_GRADIENT_STEP,
	LRG_COLOR_GRADIENT_SMOOTH
} LrgColorGradientMode;

/**
 * lrg_color_gradient_mode_get_type:
 *
 * Returns: the #GType of #LrgColorGradientMode
 */
LRG_AVAILABLE_IN_ALL
GType lrg_color_gradient_mode_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_COLOR_GRADIENT_MODE (lrg_color_gradient_mode_get_type ())

/**
 * LrgColorGradientError:
 * @LRG_COLOR_GRADIENT_ERROR_PARSE: the text form is malformed
 *
 * Errors of lrg_color_gradient_new_from_string().
 */
typedef enum
{
	LRG_COLOR_GRADIENT_ERROR_PARSE
} LrgColorGradientError;

/**
 * lrg_color_gradient_error_quark:
 *
 * Returns: the error domain of #LrgColorGradientError
 */
LRG_AVAILABLE_IN_ALL
GQuark lrg_color_gradient_error_quark (void);
#define LRG_COLOR_GRADIENT_ERROR (lrg_color_gradient_error_quark ())

#define LRG_TYPE_COLOR_GRADIENT (lrg_color_gradient_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgColorGradient, lrg_color_gradient, LRG, COLOR_GRADIENT, GObject)

/**
 * lrg_color_gradient_new:
 *
 * Creates an empty gradient.  An empty gradient evaluates to opaque
 * white everywhere (the neutral value of a colour multiplier); a single
 * stop gives its colour everywhere.
 *
 * Returns: (transfer full): a new #LrgColorGradient
 */
LRG_AVAILABLE_IN_ALL
LrgColorGradient * lrg_color_gradient_new (void);

/**
 * lrg_color_gradient_new_two:
 * @start: (array fixed-size=4): RGBA at offset 0, 0..1 each
 * @end: (array fixed-size=4): RGBA at offset 1, 0..1 each
 *
 * Returns: (transfer full): a two-stop linear gradient
 */
LRG_AVAILABLE_IN_ALL
LrgColorGradient * lrg_color_gradient_new_two (const gfloat *start,
                                               const gfloat *end);

/**
 * lrg_color_gradient_new_from_string:
 * @text: stops as "offset #rrggbb[aa]; offset #rrggbb[aa]; ..." (the
 *   format of lrg_gradient_stops_to_string(); `#rgb` is accepted too),
 *   optionally prefixed with a mode and a colon ("smooth: 0 #fff; 1 #000")
 * @error: (nullable): return location for a #LRG_COLOR_GRADIENT_ERROR
 *
 * Parses the text form.  An empty string gives an empty gradient.
 *
 * Returns: (transfer full) (nullable): the gradient, or %NULL on error
 */
LRG_AVAILABLE_IN_ALL
LrgColorGradient * lrg_color_gradient_new_from_string (const gchar  *text,
                                                       GError      **error);

/**
 * lrg_color_gradient_new_from_stops:
 * @stops: an #LrgGradientStops (the editor's gradient value type)
 *
 * Returns: (transfer full): a linear gradient with the same stops
 */
LRG_AVAILABLE_IN_ALL
LrgColorGradient * lrg_color_gradient_new_from_stops (const LrgGradientStops *stops);

/**
 * lrg_color_gradient_to_stops:
 * @self: an #LrgColorGradient
 *
 * Converts to the editor's value type (8 bits per channel; the mode is
 * not part of #LrgGradientStops).
 *
 * Returns: (transfer full): the stops
 */
LRG_AVAILABLE_IN_ALL
LrgGradientStops * lrg_color_gradient_to_stops (LrgColorGradient *self);

/**
 * lrg_color_gradient_set_from_stops:
 * @self: an #LrgColorGradient
 * @stops: (nullable): new stops; %NULL clears the gradient
 *
 * Replaces every stop with those of @stops, keeping the mode.  Emits
 * #LrgColorGradient::changed once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_gradient_set_from_stops (LrgColorGradient       *self,
                                        const LrgGradientStops *stops);

/**
 * lrg_color_gradient_to_string:
 * @self: an #LrgColorGradient
 *
 * Writes the text form: "offset #rrggbbaa; ..." with offsets in their
 * shortest exact form, prefixed with "step: " or "smooth: " for those
 * modes.  lrg_color_gradient_new_from_string() reads it back exactly
 * (colours are stored at 8 bits per channel precision in the text).
 *
 * Returns: (transfer full): the text
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_color_gradient_to_string (LrgColorGradient *self);

/**
 * lrg_color_gradient_copy:
 * @self: an #LrgColorGradient
 *
 * Returns: (transfer full): a deep copy (stops and mode)
 */
LRG_AVAILABLE_IN_ALL
LrgColorGradient * lrg_color_gradient_copy (LrgColorGradient *self);

/**
 * lrg_color_gradient_equal:
 * @a: (nullable): an #LrgColorGradient
 * @b: (nullable): an #LrgColorGradient
 *
 * Returns: %TRUE if both have the same mode and stops (two %NULLs are
 *   equal)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_color_gradient_equal (LrgColorGradient *a,
                                   LrgColorGradient *b);

/**
 * lrg_color_gradient_get_mode:
 * @self: an #LrgColorGradient
 *
 * Returns: the interpolation mode
 */
LRG_AVAILABLE_IN_ALL
LrgColorGradientMode lrg_color_gradient_get_mode (LrgColorGradient *self);

/**
 * lrg_color_gradient_set_mode:
 * @self: an #LrgColorGradient
 * @mode: the interpolation mode
 *
 * Sets #LrgColorGradient:mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_gradient_set_mode (LrgColorGradient     *self,
                                  LrgColorGradientMode  mode);

/**
 * lrg_color_gradient_get_n_stops:
 * @self: an #LrgColorGradient
 *
 * Returns: the number of stops
 */
LRG_AVAILABLE_IN_ALL
guint lrg_color_gradient_get_n_stops (LrgColorGradient *self);

/**
 * lrg_color_gradient_get_stop:
 * @self: an #LrgColorGradient
 * @index: a stop index, in offset order
 * @offset: (out) (optional): the stop's offset, 0..1
 * @color: (out caller-allocates) (array fixed-size=4) (optional): the
 *   stop's RGBA
 *
 * Returns: %TRUE if @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_color_gradient_get_stop (LrgColorGradient *self,
                                      guint             index,
                                      gfloat           *offset,
                                      gfloat           *color);

/**
 * lrg_color_gradient_add_stop:
 * @self: an #LrgColorGradient
 * @offset: offset, clamped to 0..1
 * @color: (array fixed-size=4): RGBA, each clamped to 0..1
 *
 * Inserts a stop, keeping the list sorted.  A stop at an equal offset
 * goes after the existing one (a hard edge).
 *
 * Returns: the index of the new stop
 */
LRG_AVAILABLE_IN_ALL
guint lrg_color_gradient_add_stop (LrgColorGradient *self,
                                   gfloat            offset,
                                   const gfloat     *color);

/**
 * lrg_color_gradient_set_stop:
 * @self: an #LrgColorGradient
 * @index: a stop index
 * @offset: new offset, clamped to 0..1
 * @color: (array fixed-size=4) (nullable): new RGBA, %NULL keeps it
 *
 * Moves and/or recolours a stop.  The list is re-sorted, so the stop
 * may change index.
 *
 * Returns: the stop's index after sorting, or -1 if @index is invalid
 */
LRG_AVAILABLE_IN_ALL
gint lrg_color_gradient_set_stop (LrgColorGradient *self,
                                  guint             index,
                                  gfloat            offset,
                                  const gfloat     *color);

/**
 * lrg_color_gradient_remove_stop:
 * @self: an #LrgColorGradient
 * @index: a stop index
 *
 * Returns: %TRUE if a stop was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_color_gradient_remove_stop (LrgColorGradient *self,
                                         guint             index);

/**
 * lrg_color_gradient_clear:
 * @self: an #LrgColorGradient
 *
 * Removes every stop.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_gradient_clear (LrgColorGradient *self);

/**
 * lrg_color_gradient_evaluate:
 * @self: an #LrgColorGradient
 * @t: position, clamped to 0..1
 * @out: (out caller-allocates) (array fixed-size=4): the RGBA at @t
 *
 * Samples the gradient exactly: before the first stop its colour, after
 * the last its colour, in between a blend of the two neighbouring stops
 * by the mode.  An empty gradient gives opaque white.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_gradient_evaluate (LrgColorGradient *self,
                                  gfloat            t,
                                  gfloat           *out);

/**
 * lrg_color_gradient_evaluate_fast:
 * @self: an #LrgColorGradient
 * @t: position, clamped to 0..1
 * @out: (out caller-allocates) (array fixed-size=4): the RGBA at @t
 *
 * Samples a %LRG_COLOR_GRADIENT_LUT_SIZE entry table baked on first use
 * after a change, blending the two nearest entries.  Exact for linear
 * gradients whose stops sit on table entries, within 1/255 elsewhere;
 * the per-particle path of the particle modules.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_gradient_evaluate_fast (LrgColorGradient *self,
                                       gfloat            t,
                                       gfloat           *out);

/**
 * lrg_color_gradient_evaluate_color:
 * @self: an #LrgColorGradient
 * @t: position, clamped to 0..1
 * @out: (out caller-allocates): the colour at @t, 8 bits per channel
 *
 * lrg_color_gradient_evaluate() as a #GrlColor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_color_gradient_evaluate_color (LrgColorGradient *self,
                                        gfloat            t,
                                        GrlColor         *out);

G_END_DECLS
