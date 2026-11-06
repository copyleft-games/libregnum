/* lrg-color-picker.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColorPicker: Pick a colour: saturation/value square, hue and alpha strips, hex entry, swatches.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_COLOR_PICKER (lrg_color_picker_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgColorPicker, lrg_color_picker, LRG, COLOR_PICKER, LrgWidget)

/**
 * lrg_color_picker_new:
 *
 * Returns: (transfer full): a new #LrgColorPicker
 */
LRG_AVAILABLE_IN_ALL
LrgColorPicker * lrg_color_picker_new (void);

G_END_DECLS
