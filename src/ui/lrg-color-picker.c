/* lrg-color-picker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColorPicker: Pick a colour: saturation/value square, hue and alpha strips, hex entry, swatches.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-color-picker.h"
#include "../lrg-log.h"

struct _LrgColorPicker
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgColorPicker, lrg_color_picker, LRG_TYPE_WIDGET)

static void
lrg_color_picker_class_init (LrgColorPickerClass *klass)
{
}

static void
lrg_color_picker_init (LrgColorPicker *self)
{
}

LrgColorPicker *
lrg_color_picker_new (void)
{
	return g_object_new (LRG_TYPE_COLOR_PICKER, NULL);
}
