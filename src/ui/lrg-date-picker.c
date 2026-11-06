/* lrg-date-picker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDatePicker: A date field that opens a calendar popup.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-date-picker.h"
#include "../lrg-log.h"

struct _LrgDatePicker
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgDatePicker, lrg_date_picker, LRG_TYPE_WIDGET)

static void
lrg_date_picker_class_init (LrgDatePickerClass *klass)
{
}

static void
lrg_date_picker_init (LrgDatePicker *self)
{
}

LrgDatePicker *
lrg_date_picker_new (void)
{
	return g_object_new (LRG_TYPE_DATE_PICKER, NULL);
}
