/* lrg-time-picker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTimePicker: A time field with hour/minute (and optional second) spinners.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-time-picker.h"
#include "../lrg-log.h"

struct _LrgTimePicker
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgTimePicker, lrg_time_picker, LRG_TYPE_WIDGET)

static void
lrg_time_picker_class_init (LrgTimePickerClass *klass)
{
}

static void
lrg_time_picker_init (LrgTimePicker *self)
{
}

LrgTimePicker *
lrg_time_picker_new (void)
{
	return g_object_new (LRG_TYPE_TIME_PICKER, NULL);
}
