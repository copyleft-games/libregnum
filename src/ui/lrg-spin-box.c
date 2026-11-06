/* lrg-spin-box.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpinBox: A numeric field with step buttons and drag-to-scrub.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-spin-box.h"
#include "../lrg-log.h"

struct _LrgSpinBox
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSpinBox, lrg_spin_box, LRG_TYPE_WIDGET)

static void
lrg_spin_box_class_init (LrgSpinBoxClass *klass)
{
}

static void
lrg_spin_box_init (LrgSpinBox *self)
{
}

LrgSpinBox *
lrg_spin_box_new (void)
{
	return g_object_new (LRG_TYPE_SPIN_BOX, NULL);
}
