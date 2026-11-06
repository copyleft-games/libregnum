/* lrg-time-picker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTimePicker: A time field with hour/minute (and optional second) spinners.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_TIME_PICKER (lrg_time_picker_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTimePicker, lrg_time_picker, LRG, TIME_PICKER, LrgWidget)

/**
 * lrg_time_picker_new:
 *
 * Returns: (transfer full): a new #LrgTimePicker
 */
LRG_AVAILABLE_IN_ALL
LrgTimePicker * lrg_time_picker_new (void);

G_END_DECLS
