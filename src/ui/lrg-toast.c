/* lrg-toast.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgToast: A short notification card.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-toast.h"
#include "../lrg-log.h"

struct _LrgToast
{
	LrgWidget parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgToast, lrg_toast, LRG_TYPE_WIDGET)

static void
lrg_toast_class_init (LrgToastClass *klass)
{
}

static void
lrg_toast_init (LrgToast *self)
{
}

LrgToast *
lrg_toast_new (void)
{
	return g_object_new (LRG_TYPE_TOAST, NULL);
}
