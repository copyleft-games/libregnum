/* lrg-combo-box.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgComboBox: A button that opens a popup list to pick one of several items.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-combo-box.h"
#include "../lrg-log.h"

struct _LrgComboBox
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgComboBox, lrg_combo_box, LRG_TYPE_WIDGET)

static void
lrg_combo_box_class_init (LrgComboBoxClass *klass)
{
}

static void
lrg_combo_box_init (LrgComboBox *self)
{
}

LrgComboBox *
lrg_combo_box_new (void)
{
	return g_object_new (LRG_TYPE_COMBO_BOX, NULL);
}
