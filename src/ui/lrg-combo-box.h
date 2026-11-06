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
 * Returns: (transfer full): a new #LrgComboBox
 */
LRG_AVAILABLE_IN_ALL
LrgComboBox * lrg_combo_box_new (void);

G_END_DECLS
