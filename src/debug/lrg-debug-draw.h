/* lrg-debug-draw.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDebugDraw: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_DEBUG_DRAW (lrg_debug_draw_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDebugDraw, lrg_debug_draw, LRG, DEBUG_DRAW, GObject)

/**
 * lrg_debug_draw_new:
 *
 * Returns: (transfer full): a new #LrgDebugDraw
 */
LRG_AVAILABLE_IN_ALL
LrgDebugDraw * lrg_debug_draw_new (void);

G_END_DECLS
