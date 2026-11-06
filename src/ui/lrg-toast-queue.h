/* lrg-toast-queue.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgToastQueue: Shows LrgToast messages in a corner, one at a time or stacked.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_TOAST_QUEUE (lrg_toast_queue_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgToastQueue, lrg_toast_queue, LRG, TOAST_QUEUE, LrgContainer)

/**
 * lrg_toast_queue_new:
 *
 * Returns: (transfer full): a new #LrgToastQueue
 */
LRG_AVAILABLE_IN_ALL
LrgToastQueue * lrg_toast_queue_new (void);

G_END_DECLS
