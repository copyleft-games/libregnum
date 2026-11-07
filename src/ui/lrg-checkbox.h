/* lrg-checkbox.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Checkbox widget with toggle state and optional label.
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

#define LRG_TYPE_CHECKBOX (lrg_checkbox_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCheckbox, lrg_checkbox, LRG, CHECKBOX, LrgWidget)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_checkbox_new:
 * @label: (nullable): optional label text
 *
 * Creates a new checkbox widget.
 *
 * Returns: (transfer full): A new #LrgCheckbox
 */
LRG_AVAILABLE_IN_ALL
LrgCheckbox * lrg_checkbox_new (const gchar *label);

/**
 * lrg_checkbox_new_with_style:
 * @label: (nullable): optional label text
 * @style: how the indicator is drawn
 *
 * Creates a new checkbox drawn as a check box, a radio button or a
 * sliding switch.
 *
 * Returns: (transfer full): A new #LrgCheckbox
 */
LRG_AVAILABLE_IN_ALL
LrgCheckbox * lrg_checkbox_new_with_style (const gchar      *label,
                                           LrgCheckboxStyle  style);

/* ==========================================================================
 * State
 * ========================================================================== */

/**
 * lrg_checkbox_get_checked:
 * @self: an #LrgCheckbox
 *
 * Gets whether the checkbox is checked.
 *
 * Returns: %TRUE if checked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_checkbox_get_checked (LrgCheckbox *self);

/**
 * lrg_checkbox_set_checked:
 * @self: an #LrgCheckbox
 * @checked: the new checked state
 *
 * Sets the checkbox's checked state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_set_checked (LrgCheckbox *self,
                               gboolean     checked);

/**
 * lrg_checkbox_toggle:
 * @self: an #LrgCheckbox
 *
 * Toggles the checkbox's checked state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_toggle (LrgCheckbox *self);

/**
 * lrg_checkbox_get_inconsistent:
 * @self: an #LrgCheckbox
 *
 * Returns: %TRUE while the indeterminate (mixed) dash is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_checkbox_get_inconsistent (LrgCheckbox *self);

/**
 * lrg_checkbox_set_inconsistent:
 * @self: an #LrgCheckbox
 * @inconsistent: %TRUE to show the indeterminate state
 *
 * Shows a dash instead of the tick (a centred knob for the switch
 * style), for a value that is neither on nor off, such as a flag that
 * differs between several edited objects. Independent of the checked
 * state; lrg_checkbox_toggle() (a click, ACTIVATE) clears it and checks
 * the box, emitting #LrgCheckbox::toggled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_set_inconsistent (LrgCheckbox *self,
                                    gboolean     inconsistent);

/**
 * lrg_checkbox_get_check_progress:
 * @self: an #LrgCheckbox
 *
 * The check animation: 0 when fully unchecked, 1 when fully checked,
 * in between (and briefly beyond, with bouncy motion) while the mark
 * draws in or the switch knob slides.
 *
 * Returns: the animation progress
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_checkbox_get_check_progress (LrgCheckbox *self);

/* ==========================================================================
 * Style
 * ========================================================================== */

/**
 * lrg_checkbox_get_style:
 * @self: an #LrgCheckbox
 *
 * Returns: how the indicator is drawn
 */
LRG_AVAILABLE_IN_ALL
LrgCheckboxStyle lrg_checkbox_get_style (LrgCheckbox *self);

/**
 * lrg_checkbox_set_style:
 * @self: an #LrgCheckbox
 * @style: %LRG_CHECKBOX_STYLE_CHECK, %LRG_CHECKBOX_STYLE_RADIO or
 *   %LRG_CHECKBOX_STYLE_SWITCH
 *
 * Changes how the indicator is drawn. A switch is wider than a box,
 * so this queues a layout.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_set_style (LrgCheckbox      *self,
                             LrgCheckboxStyle  style);

/* ==========================================================================
 * Label
 * ========================================================================== */

/**
 * lrg_checkbox_get_label:
 * @self: an #LrgCheckbox
 *
 * Gets the checkbox's label text.
 *
 * Returns: (transfer none) (nullable): The label text
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_checkbox_get_label (LrgCheckbox *self);

/**
 * lrg_checkbox_set_label:
 * @self: an #LrgCheckbox
 * @label: (nullable): the label text
 *
 * Sets the checkbox's label text.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_set_label (LrgCheckbox *self,
                             const gchar *label);

/* ==========================================================================
 * Appearance
 * ========================================================================== */

/**
 * lrg_checkbox_get_box_size:
 * @self: an #LrgCheckbox
 *
 * Gets the checkbox box size.
 *
 * Returns: The box size in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_checkbox_get_box_size (LrgCheckbox *self);

/**
 * lrg_checkbox_set_box_size:
 * @self: an #LrgCheckbox
 * @size: the box size in pixels
 *
 * Sets the checkbox box size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_set_box_size (LrgCheckbox *self,
                                gfloat       size);

/**
 * lrg_checkbox_get_spacing:
 * @self: an #LrgCheckbox
 *
 * Gets the spacing between box and label.
 *
 * Returns: The spacing in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_checkbox_get_spacing (LrgCheckbox *self);

/**
 * lrg_checkbox_set_spacing:
 * @self: an #LrgCheckbox
 * @spacing: the spacing in pixels
 *
 * Sets the spacing between box and label.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_set_spacing (LrgCheckbox *self,
                               gfloat       spacing);

/**
 * lrg_checkbox_get_box_color:
 * @self: an #LrgCheckbox
 *
 * Gets the checkbox box color.
 *
 * Returns: (transfer none): The box color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_checkbox_get_box_color (LrgCheckbox *self);

/**
 * lrg_checkbox_set_box_color:
 * @self: an #LrgCheckbox
 * @color: the box color
 *
 * Sets the checkbox box color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_set_box_color (LrgCheckbox    *self,
                                 const GrlColor *color);

/**
 * lrg_checkbox_get_check_color:
 * @self: an #LrgCheckbox
 *
 * Gets the checkmark color.
 *
 * Returns: (transfer none): The check color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_checkbox_get_check_color (LrgCheckbox *self);

/**
 * lrg_checkbox_set_check_color:
 * @self: an #LrgCheckbox
 * @color: the checkmark color
 *
 * Sets the checkmark color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_set_check_color (LrgCheckbox    *self,
                                   const GrlColor *color);

/**
 * lrg_checkbox_get_text_color:
 * @self: an #LrgCheckbox
 *
 * Gets the label text color.
 *
 * Returns: (transfer none): The text color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_checkbox_get_text_color (LrgCheckbox *self);

/**
 * lrg_checkbox_set_text_color:
 * @self: an #LrgCheckbox
 * @color: the text color
 *
 * Sets the label text color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_set_text_color (LrgCheckbox    *self,
                                  const GrlColor *color);

/**
 * lrg_checkbox_get_font_size:
 * @self: an #LrgCheckbox
 *
 * Gets the label font size.
 *
 * Returns: The font size in pixels
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_checkbox_get_font_size (LrgCheckbox *self);

/**
 * lrg_checkbox_set_font_size:
 * @self: an #LrgCheckbox
 * @size: the font size in pixels
 *
 * Sets the label font size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_checkbox_set_font_size (LrgCheckbox *self,
                                 gfloat       size);

G_END_DECLS
