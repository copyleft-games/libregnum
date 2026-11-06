/* lrg-wrap-box.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWrapBox: A box that flows children into lines, wrapping when a line is full.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_WRAP_BOX (lrg_wrap_box_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWrapBox, lrg_wrap_box, LRG, WRAP_BOX, LrgContainer)

/**
 * lrg_wrap_box_new:
 *
 * Returns: (transfer full): a new #LrgWrapBox
 */
LRG_AVAILABLE_IN_ALL
LrgWrapBox * lrg_wrap_box_new (void);

G_END_DECLS
