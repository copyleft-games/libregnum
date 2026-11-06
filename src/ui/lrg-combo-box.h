/* lrg-combo-box.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgComboBox: A button that opens a popup list to pick one of several items.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_COMBO_BOX (lrg_combo_box_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgComboBox, lrg_combo_box, LRG, COMBO_BOX, LrgWidget)

/**
 * lrg_combo_box_new:
 *
 * Creates an empty combo box with nothing selected.
 *
 * Returns: (transfer full): a new #LrgComboBox
 */
LRG_AVAILABLE_IN_ALL
LrgComboBox * lrg_combo_box_new (void);

/**
 * lrg_combo_box_append:
 * @self: an #LrgComboBox
 * @text: the item's label
 *
 * Adds an item at the end of the list. Closes the popup if it is open.
 */
LRG_AVAILABLE_IN_ALL
void lrg_combo_box_append (LrgComboBox *self,
                           const gchar *text);

/**
 * lrg_combo_box_remove_all:
 * @self: an #LrgComboBox
 *
 * Removes every item and clears the selection (emitting
 * #LrgComboBox::changed if something was selected).
 */
LRG_AVAILABLE_IN_ALL
void lrg_combo_box_remove_all (LrgComboBox *self);

/**
 * lrg_combo_box_get_n_items:
 * @self: an #LrgComboBox
 *
 * Returns: the number of items
 */
LRG_AVAILABLE_IN_ALL
guint lrg_combo_box_get_n_items (LrgComboBox *self);

/**
 * lrg_combo_box_get_item_text:
 * @self: an #LrgComboBox
 * @index: an item index
 *
 * Returns: (transfer none) (nullable): the label of item @index, or
 *   %NULL if @index is out of range
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_combo_box_get_item_text (LrgComboBox *self,
                                           guint        index);

/**
 * lrg_combo_box_get_selected:
 * @self: an #LrgComboBox
 *
 * Returns: the selected item index, or -1 for none
 */
LRG_AVAILABLE_IN_ALL
gint lrg_combo_box_get_selected (LrgComboBox *self);

/**
 * lrg_combo_box_set_selected:
 * @self: an #LrgComboBox
 * @index: an item index, or -1 to select nothing
 *
 * Selects an item. Out-of-range indices select nothing. Emits
 * #LrgComboBox::changed when the selection changes.
 */
LRG_AVAILABLE_IN_ALL
void lrg_combo_box_set_selected (LrgComboBox *self,
                                 gint         index);

/**
 * lrg_combo_box_get_selected_text:
 * @self: an #LrgComboBox
 *
 * Returns: (transfer none) (nullable): the selected item's label, or
 *   %NULL when nothing is selected
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_combo_box_get_selected_text (LrgComboBox *self);

/**
 * lrg_combo_box_get_placeholder:
 * @self: an #LrgComboBox
 *
 * Returns: (transfer none) (nullable): the text shown when nothing is
 *   selected
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_combo_box_get_placeholder (LrgComboBox *self);

/**
 * lrg_combo_box_set_placeholder:
 * @self: an #LrgComboBox
 * @placeholder: (nullable): text shown, muted, when nothing is selected
 */
LRG_AVAILABLE_IN_ALL
void lrg_combo_box_set_placeholder (LrgComboBox *self,
                                    const gchar *placeholder);

/**
 * lrg_combo_box_popup:
 * @self: an #LrgComboBox
 *
 * Opens the item list below the combo box. The combo box must be
 * inside an #LrgCanvas. Does nothing if the list is already open.
 */
LRG_AVAILABLE_IN_ALL
void lrg_combo_box_popup (LrgComboBox *self);

/**
 * lrg_combo_box_popdown:
 * @self: an #LrgComboBox
 *
 * Closes the item list if it is open.
 */
LRG_AVAILABLE_IN_ALL
void lrg_combo_box_popdown (LrgComboBox *self);

/**
 * lrg_combo_box_get_popup_open:
 * @self: an #LrgComboBox
 *
 * Returns: %TRUE while the item list is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_combo_box_get_popup_open (LrgComboBox *self);

G_END_DECLS
