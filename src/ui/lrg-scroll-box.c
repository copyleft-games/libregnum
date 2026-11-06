/* lrg-scroll-box.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScrollBox: A clipped, scrollable viewport onto one child with spring-smoothed scrolling.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-scroll-box.h"
#include "../lrg-log.h"

struct _LrgScrollBox
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgScrollBox, lrg_scroll_box, LRG_TYPE_CONTAINER)

static void
lrg_scroll_box_class_init (LrgScrollBoxClass *klass)
{
}

static void
lrg_scroll_box_init (LrgScrollBox *self)
{
}

LrgScrollBox *
lrg_scroll_box_new (void)
{
	return g_object_new (LRG_TYPE_SCROLL_BOX, NULL);
}
