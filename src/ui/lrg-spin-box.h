/* lrg-spin-box.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpinBox: A numeric field with step buttons and drag-to-scrub.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_SPIN_BOX (lrg_spin_box_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpinBox, lrg_spin_box, LRG, SPIN_BOX, LrgWidget)

/**
 * lrg_spin_box_new:
 *
 * Creates a spin box for values 0..100 in steps of 1, shown without
 * decimals.
 *
 * Returns: (transfer full): a new #LrgSpinBox
 */
LRG_AVAILABLE_IN_ALL
LrgSpinBox * lrg_spin_box_new (void);

/**
 * lrg_spin_box_new_with_range:
 * @min: smallest value
 * @max: largest value
 * @step: amount one click, key press or wheel notch changes the value
 *
 * Returns: (transfer full): a new #LrgSpinBox starting at @min
 */
LRG_AVAILABLE_IN_ALL
LrgSpinBox * lrg_spin_box_new_with_range (gdouble min,
                                          gdouble max,
                                          gdouble step);

/**
 * lrg_spin_box_get_value:
 * @self: an #LrgSpinBox
 *
 * Returns: the current value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_spin_box_get_value (LrgSpinBox *self);

/**
 * lrg_spin_box_set_value:
 * @self: an #LrgSpinBox
 * @value: the new value, clamped to [min, max]
 *
 * Emits #LrgSpinBox::value-changed when the value actually changes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spin_box_set_value (LrgSpinBox *self,
                             gdouble     value);

/**
 * lrg_spin_box_get_min:
 * @self: an #LrgSpinBox
 *
 * Returns: the smallest value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_spin_box_get_min (LrgSpinBox *self);

/**
 * lrg_spin_box_set_min:
 * @self: an #LrgSpinBox
 * @min: the smallest value; the current value is clamped
 */
LRG_AVAILABLE_IN_ALL
void lrg_spin_box_set_min (LrgSpinBox *self,
                           gdouble     min);

/**
 * lrg_spin_box_get_max:
 * @self: an #LrgSpinBox
 *
 * Returns: the largest value
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_spin_box_get_max (LrgSpinBox *self);

/**
 * lrg_spin_box_set_max:
 * @self: an #LrgSpinBox
 * @max: the largest value; the current value is clamped
 */
LRG_AVAILABLE_IN_ALL
void lrg_spin_box_set_max (LrgSpinBox *self,
                           gdouble     max);

/**
 * lrg_spin_box_set_range:
 * @self: an #LrgSpinBox
 * @min: the smallest value
 * @max: the largest value (>= @min)
 */
LRG_AVAILABLE_IN_ALL
void lrg_spin_box_set_range (LrgSpinBox *self,
                             gdouble     min,
                             gdouble     max);

/**
 * lrg_spin_box_get_step:
 * @self: an #LrgSpinBox
 *
 * Returns: the step increment
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_spin_box_get_step (LrgSpinBox *self);

/**
 * lrg_spin_box_set_step:
 * @self: an #LrgSpinBox
 * @step: amount one click, key press, wheel notch or 6 units of
 *   scrubbing changes the value (Page Up / Page Down use ten steps)
 */
LRG_AVAILABLE_IN_ALL
void lrg_spin_box_set_step (LrgSpinBox *self,
                            gdouble     step);

/**
 * lrg_spin_box_get_digits:
 * @self: an #LrgSpinBox
 *
 * Returns: number of decimals shown
 */
LRG_AVAILABLE_IN_ALL
guint lrg_spin_box_get_digits (LrgSpinBox *self);

/**
 * lrg_spin_box_set_digits:
 * @self: an #LrgSpinBox
 * @digits: number of decimals shown (0..10)
 */
LRG_AVAILABLE_IN_ALL
void lrg_spin_box_set_digits (LrgSpinBox *self,
                              guint       digits);

/**
 * lrg_spin_box_get_text:
 * @self: an #LrgSpinBox
 *
 * Returns: (transfer full): the value formatted as displayed
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_spin_box_get_text (LrgSpinBox *self);

/**
 * lrg_spin_box_spin:
 * @self: an #LrgSpinBox
 * @steps: number of steps to move (negative moves down)
 *
 * Changes the value by @steps times the step, clamped to the range.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spin_box_spin (LrgSpinBox *self,
                        gint        steps);

/**
 * lrg_spin_box_get_scrubbing:
 * @self: an #LrgSpinBox
 *
 * Returns: %TRUE between #LrgSpinBox::scrub-begin and
 *   #LrgSpinBox::scrub-end (a press on the value or a step button)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spin_box_get_scrubbing (LrgSpinBox *self);

/**
 * lrg_spin_box_cancel_scrub:
 * @self: an #LrgSpinBox
 *
 * Cancels the press gesture in progress, as Escape does: the value goes
 * back to what it was at #LrgSpinBox::scrub-begin, the rest of the drag
 * is ignored and #LrgSpinBox::scrub-end is emitted with @commit %FALSE.
 * Does nothing outside a gesture.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spin_box_cancel_scrub (LrgSpinBox *self);

G_END_DECLS
