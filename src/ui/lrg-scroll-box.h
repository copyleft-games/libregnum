/* lrg-scroll-box.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScrollBox: A clipped, scrollable viewport onto one child with spring-smoothed scrolling.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCROLL_BOX (lrg_scroll_box_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScrollBox, lrg_scroll_box, LRG, SCROLL_BOX, LrgContainer)

/**
 * lrg_scroll_box_new:
 *
 * Returns: (transfer full): a new #LrgScrollBox
 */
LRG_AVAILABLE_IN_ALL
LrgScrollBox * lrg_scroll_box_new (void);

G_END_DECLS
