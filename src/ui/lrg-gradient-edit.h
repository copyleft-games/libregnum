/* lrg-gradient-edit.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGradientStops: a sorted list of colour stops (boxed value type).
 * LrgGradientEdit: a gradient bar with draggable stops and a colour
 * picker for the selected stop.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgGradientStops
 * ========================================================================== */

#define LRG_TYPE_GRADIENT_STOPS (lrg_gradient_stops_get_type ())

/**
 * LrgGradientStops:
 *
 * An opaque list of colour stops (offset 0..1 and a #GrlColor), kept
 * sorted by offset. Used as a property value type: a #GParamSpecBoxed
 * of %LRG_TYPE_GRADIENT_STOPS gets an #LrgGradientEdit in the property
 * grid. An empty list evaluates to transparent black, a single stop to
 * its colour everywhere.
 */
typedef struct _LrgGradientStops LrgGradientStops;

/**
 * lrg_gradient_stops_get_type:
 *
 * Returns: the #GType of #LrgGradientStops
 */
LRG_AVAILABLE_IN_ALL
GType lrg_gradient_stops_get_type (void) G_GNUC_CONST;

/**
 * lrg_gradient_stops_new:
 *
 * Returns: (transfer full): an empty stop list
 */
LRG_AVAILABLE_IN_ALL
LrgGradientStops * lrg_gradient_stops_new (void);

/**
 * lrg_gradient_stops_new_two:
 * @start: colour at offset 0
 * @end: colour at offset 1
 *
 * Returns: (transfer full): a two-stop gradient from @start to @end
 */
LRG_AVAILABLE_IN_ALL
LrgGradientStops * lrg_gradient_stops_new_two (const GrlColor *start,
                                               const GrlColor *end);

/**
 * lrg_gradient_stops_new_from_string:
 * @text: stops as written by lrg_gradient_stops_to_string():
 *   "0 #ff0000ff; 0.5 #00ff00; 1 #0000ff"
 *
 * Returns: (transfer full) (nullable): the parsed stops, or %NULL if
 *   @text is malformed
 */
LRG_AVAILABLE_IN_ALL
LrgGradientStops * lrg_gradient_stops_new_from_string (const gchar *text);

/**
 * lrg_gradient_stops_copy:
 * @self: an #LrgGradientStops
 *
 * Returns: (transfer full): a deep copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgGradientStops * lrg_gradient_stops_copy (const LrgGradientStops *self);

/**
 * lrg_gradient_stops_free:
 * @self: an #LrgGradientStops
 *
 * Frees @self.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gradient_stops_free (LrgGradientStops *self);

/**
 * lrg_gradient_stops_equal:
 * @a: (nullable): an #LrgGradientStops
 * @b: (nullable): an #LrgGradientStops
 *
 * Returns: %TRUE if both have the same stops (two %NULLs are equal)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gradient_stops_equal (const LrgGradientStops *a,
                                   const LrgGradientStops *b);

/**
 * lrg_gradient_stops_get_n_stops:
 * @self: an #LrgGradientStops
 *
 * Returns: the number of stops
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gradient_stops_get_n_stops (const LrgGradientStops *self);

/**
 * lrg_gradient_stops_get_stop:
 * @self: an #LrgGradientStops
 * @index: a stop index, in offset order
 * @offset: (out) (optional): the stop's offset, 0..1
 * @color: (out caller-allocates) (optional): the stop's colour
 *
 * Returns: %TRUE if @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gradient_stops_get_stop (const LrgGradientStops *self,
                                      guint                   index,
                                      gfloat                 *offset,
                                      GrlColor               *color);

/**
 * lrg_gradient_stops_add:
 * @self: an #LrgGradientStops
 * @offset: offset, clamped to 0..1
 * @color: colour
 *
 * Inserts a stop, keeping the list sorted. A stop at an equal offset
 * goes after the existing one (a hard edge).
 *
 * Returns: the index of the new stop
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gradient_stops_add (LrgGradientStops *self,
                              gfloat            offset,
                              const GrlColor   *color);

/**
 * lrg_gradient_stops_set_stop:
 * @self: an #LrgGradientStops
 * @index: a stop index
 * @offset: new offset, clamped to 0..1
 * @color: (nullable): new colour, %NULL to keep it
 *
 * Moves and/or recolours a stop. The list is re-sorted, so the stop
 * may change index.
 *
 * Returns: the stop's index after sorting, or -1 if @index is invalid
 */
LRG_AVAILABLE_IN_ALL
gint lrg_gradient_stops_set_stop (LrgGradientStops *self,
                                  guint             index,
                                  gfloat            offset,
                                  const GrlColor   *color);

/**
 * lrg_gradient_stops_remove:
 * @self: an #LrgGradientStops
 * @index: a stop index
 *
 * Returns: %TRUE if a stop was removed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gradient_stops_remove (LrgGradientStops *self,
                                    guint             index);

/**
 * lrg_gradient_stops_evaluate:
 * @self: an #LrgGradientStops
 * @t: position, clamped to 0..1
 * @color: (out caller-allocates): the colour at @t
 *
 * Samples the gradient: before the first stop its colour, after the
 * last its colour, in between a linear blend of the two neighbouring
 * stops (all four channels).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gradient_stops_evaluate (const LrgGradientStops *self,
                                  gfloat                  t,
                                  GrlColor               *color);

/**
 * lrg_gradient_stops_to_string:
 * @self: an #LrgGradientStops
 *
 * Returns: (transfer full): the stops as "offset #rrggbbaa; ...",
 *   readable by lrg_gradient_stops_new_from_string()
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_gradient_stops_to_string (const LrgGradientStops *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgGradientStops, lrg_gradient_stops_free)

/* ==========================================================================
 * LrgGradientEdit
 * ========================================================================== */

#define LRG_TYPE_GRADIENT_EDIT (lrg_gradient_edit_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGradientEdit, lrg_gradient_edit, LRG, GRADIENT_EDIT, LrgWidget)

/**
 * lrg_gradient_edit_new:
 *
 * Creates a gradient editor: a bar showing the gradient over a checker
 * pattern with a handle per stop below it. Drag a handle to move the
 * stop (drag it well below the bar to remove it, while more than two
 * remain), click the bar to add a stop with the colour found there,
 * click the selected handle again (or press Enter) to open a colour
 * picker for it, Delete removes the selected stop, Left/Right nudge
 * it. Escape during a drag puts the stops back.
 *
 * Returns: (transfer full): a new #LrgGradientEdit with a black to
 *   white gradient
 */
LRG_AVAILABLE_IN_ALL
LrgGradientEdit * lrg_gradient_edit_new (void);

/**
 * lrg_gradient_edit_get_stops:
 * @self: an #LrgGradientEdit
 *
 * Returns: (transfer none): the edited stops
 */
LRG_AVAILABLE_IN_ALL
const LrgGradientStops * lrg_gradient_edit_get_stops (LrgGradientEdit *self);

/**
 * lrg_gradient_edit_set_stops:
 * @self: an #LrgGradientEdit
 * @stops: (nullable): stops to show (copied), %NULL for black to white
 *
 * Replaces the stops without emitting #LrgGradientEdit::stops-changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gradient_edit_set_stops (LrgGradientEdit        *self,
                                  const LrgGradientStops *stops);

/**
 * lrg_gradient_edit_get_selected:
 * @self: an #LrgGradientEdit
 *
 * Returns: the selected stop index, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_gradient_edit_get_selected (LrgGradientEdit *self);

/**
 * lrg_gradient_edit_set_selected:
 * @self: an #LrgGradientEdit
 * @index: a stop index, or -1 for none
 *
 * Selects a stop.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gradient_edit_set_selected (LrgGradientEdit *self,
                                     gint             index);

/**
 * lrg_gradient_edit_get_handle_rect:
 * @self: an #LrgGradientEdit
 * @index: a stop index
 * @rect: (out caller-allocates): the handle's world rectangle
 *
 * For tests and tutorials: where the handle of a stop is.
 *
 * Returns: %TRUE if @index exists
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gradient_edit_get_handle_rect (LrgGradientEdit *self,
                                            guint            index,
                                            GrlRectangle    *rect);

/**
 * lrg_gradient_edit_get_bar_rect:
 * @self: an #LrgGradientEdit
 * @rect: (out caller-allocates): the gradient bar's world rectangle
 *
 * For tests and tutorials: where the bar is (offset 0 at its left).
 */
LRG_AVAILABLE_IN_ALL
void lrg_gradient_edit_get_bar_rect (LrgGradientEdit *self,
                                     GrlRectangle    *rect);

/**
 * lrg_gradient_edit_open_color_picker:
 * @self: an #LrgGradientEdit on an #LrgCanvas
 *
 * Opens the colour picker popup for the selected stop. Changes made
 * in it are one edit gesture, ended when the popup closes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gradient_edit_open_color_picker (LrgGradientEdit *self);

/**
 * lrg_gradient_edit_get_color_picker:
 * @self: an #LrgGradientEdit
 *
 * Returns: (transfer none) (nullable): the open colour picker popup,
 *   or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_gradient_edit_get_color_picker (LrgGradientEdit *self);

G_END_DECLS
