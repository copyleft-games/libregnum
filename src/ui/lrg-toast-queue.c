/* lrg-toast-queue.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgToastQueue: Shows LrgToast messages in a corner, one at a time or stacked.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-toast-queue.h"
#include "../lrg-log.h"

struct _LrgToastQueue
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgToastQueue, lrg_toast_queue, LRG_TYPE_CONTAINER)

static void
lrg_toast_queue_class_init (LrgToastQueueClass *klass)
{
}

static void
lrg_toast_queue_init (LrgToastQueue *self)
{
}

LrgToastQueue *
lrg_toast_queue_new (void)
{
	return g_object_new (LRG_TYPE_TOAST_QUEUE, NULL);
}
