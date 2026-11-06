/* lrg-vector-edit.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVectorEdit: per-axis scrub fields for 2, 3 or 4 component vectors.
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

/**
 * LRG_VECTOR_EDIT_MAX_COMPONENTS:
 *
 * The largest number of components an #LrgVectorEdit shows.
 */
#define LRG_VECTOR_EDIT_MAX_COMPONENTS (4)

#define LRG_TYPE_VECTOR_EDIT (lrg_vector_edit_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVectorEdit, lrg_vector_edit, LRG, VECTOR_EDIT, LrgWidget)

/**
 * lrg_vector_edit_new:
 * @n_components: 1 to 4
 *
 * Creates a row of number fields, one per axis, each with a coloured
 * axis label (X red, Y green, Z blue, W amber). Drag a field sideways
 * to scrub it (Shift slows down ten times, Ctrl speeds up ten times);
 * click it without dragging to type a number (Enter applies, Escape
 * abandons). With the uniform lock on (see
 * lrg_vector_edit_set_show_uniform()), scrubbing one axis scales the
 * others by the same ratio. A scrub is one gesture: edit-begin on the
 * first movement, value-changed every frame, edit-end on release, or
 * edit-end with commit %FALSE after CANCEL (Escape) put the values
 * back. A typed value is a one-shot gesture.
 *
 * Returns: (transfer full): a new #LrgVectorEdit at zero
 */
LRG_AVAILABLE_IN_ALL
LrgVectorEdit * lrg_vector_edit_new (guint n_components);

/**
 * lrg_vector_edit_get_n_components:
 * @self: an #LrgVectorEdit
 *
 * Returns: the number of axes shown
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vector_edit_get_n_components (LrgVectorEdit *self);

/**
 * lrg_vector_edit_set_n_components:
 * @self: an #LrgVectorEdit
 * @n_components: 1 to 4
 *
 * Changes the number of axes shown.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vector_edit_set_n_components (LrgVectorEdit *self,
                                       guint          n_components);

/**
 * lrg_vector_edit_get_component:
 * @self: an #LrgVectorEdit
 * @axis: 0 (X) to 3 (W)
 *
 * Returns: the value of @axis
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_vector_edit_get_component (LrgVectorEdit *self,
                                      guint          axis);

/**
 * lrg_vector_edit_set_component:
 * @self: an #LrgVectorEdit
 * @axis: 0 (X) to 3 (W)
 * @value: the new value, clamped to the range
 *
 * Sets one axis without emitting #LrgVectorEdit::value-changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vector_edit_set_component (LrgVectorEdit *self,
                                    guint          axis,
                                    gfloat         value);

/**
 * lrg_vector_edit_get_values:
 * @self: an #LrgVectorEdit
 * @values: (out caller-allocates) (array fixed-size=4): the four
 *   components (unused ones are 0)
 *
 * Reads every component.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vector_edit_get_values (LrgVectorEdit *self,
                                 gfloat        *values);

/**
 * lrg_vector_edit_set_values:
 * @self: an #LrgVectorEdit
 * @values: (array length=n_values): the components
 * @n_values: how many of @values to use (at most 4)
 *
 * Sets components without emitting #LrgVectorEdit::value-changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vector_edit_set_values (LrgVectorEdit *self,
                                 const gfloat  *values,
                                 guint          n_values);

/**
 * lrg_vector_edit_set_range:
 * @self: an #LrgVectorEdit
 * @min: lowest value of every axis
 * @max: highest value of every axis
 *
 * Limits scrubbing and typing. The default is unlimited.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vector_edit_set_range (LrgVectorEdit *self,
                                gdouble        min,
                                gdouble        max);

/**
 * lrg_vector_edit_get_step:
 * @self: an #LrgVectorEdit
 *
 * Returns: the change per two units of scrubbing
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_vector_edit_get_step (LrgVectorEdit *self);

/**
 * lrg_vector_edit_set_step:
 * @self: an #LrgVectorEdit
 * @step: the change per two units of horizontal drag (default 0.05)
 *
 * Sets the scrub speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vector_edit_set_step (LrgVectorEdit *self,
                               gdouble        step);

/**
 * lrg_vector_edit_get_digits:
 * @self: an #LrgVectorEdit
 *
 * Returns: the decimals shown
 */
LRG_AVAILABLE_IN_ALL
guint lrg_vector_edit_get_digits (LrgVectorEdit *self);

/**
 * lrg_vector_edit_set_digits:
 * @self: an #LrgVectorEdit
 * @digits: decimals shown (default 3)
 *
 * Sets how values are formatted.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vector_edit_set_digits (LrgVectorEdit *self,
                                 guint          digits);

/**
 * lrg_vector_edit_get_uniform:
 * @self: an #LrgVectorEdit
 *
 * Returns: %TRUE while the uniform lock is on
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vector_edit_get_uniform (LrgVectorEdit *self);

/**
 * lrg_vector_edit_set_uniform:
 * @self: an #LrgVectorEdit
 * @uniform: %TRUE to scale every axis together
 *
 * Turns the uniform lock on or off.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vector_edit_set_uniform (LrgVectorEdit *self,
                                  gboolean       uniform);

/**
 * lrg_vector_edit_get_show_uniform:
 * @self: an #LrgVectorEdit
 *
 * Returns: %TRUE if the lock button is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vector_edit_get_show_uniform (LrgVectorEdit *self);

/**
 * lrg_vector_edit_set_show_uniform:
 * @self: an #LrgVectorEdit
 * @show: %TRUE to show a lock button at the end of the row
 *
 * Shows or hides the uniform lock button (scale vectors want it).
 */
LRG_AVAILABLE_IN_ALL
void lrg_vector_edit_set_show_uniform (LrgVectorEdit *self,
                                       gboolean       show);

/**
 * lrg_vector_edit_get_axis_mixed:
 * @self: an #LrgVectorEdit
 * @axis: 0 to 3
 *
 * Returns: %TRUE if @axis shows an em dash for "several values"
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vector_edit_get_axis_mixed (LrgVectorEdit *self,
                                         guint          axis);

/**
 * lrg_vector_edit_set_axis_mixed:
 * @self: an #LrgVectorEdit
 * @axis: 0 to 3
 * @mixed: %TRUE when the edited objects disagree on this axis
 *
 * Shows an em dash instead of the number. Editing the axis clears it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_vector_edit_set_axis_mixed (LrgVectorEdit *self,
                                     guint          axis,
                                     gboolean       mixed);

/**
 * lrg_vector_edit_get_editing_axis:
 * @self: an #LrgVectorEdit
 *
 * Returns: the axis being typed into, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_vector_edit_get_editing_axis (LrgVectorEdit *self);

/**
 * lrg_vector_edit_get_field_rect:
 * @self: an #LrgVectorEdit
 * @axis: 0 to 3
 * @rect: (out caller-allocates): world rectangle of the axis field
 *
 * For tests and tutorials: where an axis field is.
 *
 * Returns: %TRUE if @axis is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vector_edit_get_field_rect (LrgVectorEdit *self,
                                         guint          axis,
                                         GrlRectangle  *rect);

/**
 * lrg_vector_edit_get_lock_rect:
 * @self: an #LrgVectorEdit
 * @rect: (out caller-allocates): world rectangle of the lock button
 *
 * Returns: %TRUE if the lock button is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_vector_edit_get_lock_rect (LrgVectorEdit *self,
                                        GrlRectangle  *rect);

G_END_DECLS
