/* lrg-wrap-box.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWrapBox: A box that flows children into lines, wrapping when a line is full.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-wrap-box.h"
#include "../lrg-log.h"

struct _LrgWrapBox
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgWrapBox, lrg_wrap_box, LRG_TYPE_CONTAINER)

static void
lrg_wrap_box_class_init (LrgWrapBoxClass *klass)
{
}

static void
lrg_wrap_box_init (LrgWrapBox *self)
{
}

LrgWrapBox *
lrg_wrap_box_new (void)
{
	return g_object_new (LRG_TYPE_WRAP_BOX, NULL);
}
