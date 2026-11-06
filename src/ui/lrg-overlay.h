/* lrg-overlay.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgOverlay: A stack of children drawn on top of each other, each placed by its own alignment and margin.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_OVERLAY (lrg_overlay_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgOverlay, lrg_overlay, LRG, OVERLAY, LrgContainer)

/**
 * lrg_overlay_new:
 *
 * Returns: (transfer full): a new #LrgOverlay
 */
LRG_AVAILABLE_IN_ALL
LrgOverlay * lrg_overlay_new (void);

G_END_DECLS
