/* lrg-toast.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgToast: A short notification card.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_TOAST (lrg_toast_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgToast, lrg_toast, LRG, TOAST, LrgWidget)

/**
 * lrg_toast_new:
 *
 * Returns: (transfer full): a new #LrgToast
 */
LRG_AVAILABLE_IN_ALL
LrgToast * lrg_toast_new (void);

G_END_DECLS
