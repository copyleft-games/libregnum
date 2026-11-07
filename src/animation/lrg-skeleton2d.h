/* lrg-skeleton2d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkeleton2D: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_SKELETON2D (lrg_skeleton2d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkeleton2D, lrg_skeleton2d, LRG, SKELETON2D, GObject)

/**
 * lrg_skeleton2d_new:
 *
 * Returns: (transfer full): a new #LrgSkeleton2D
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2D * lrg_skeleton2d_new (void);

G_END_DECLS
