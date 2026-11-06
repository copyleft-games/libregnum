/* lrg-date-picker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDatePicker: A date field that opens a calendar popup.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_DATE_PICKER (lrg_date_picker_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDatePicker, lrg_date_picker, LRG, DATE_PICKER, LrgWidget)

/**
 * lrg_date_picker_new:
 *
 * Returns: (transfer full): a new #LrgDatePicker
 */
LRG_AVAILABLE_IN_ALL
LrgDatePicker * lrg_date_picker_new (void);

G_END_DECLS
