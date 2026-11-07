/* lrg-bake-bvh.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBakeBvh: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_BAKE_BVH (lrg_bake_bvh_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBakeBvh, lrg_bake_bvh, LRG, BAKE_BVH, GObject)

/**
 * lrg_bake_bvh_new:
 *
 * Returns: (transfer full): a new #LrgBakeBvh
 */
LRG_AVAILABLE_IN_ALL
LrgBakeBvh * lrg_bake_bvh_new (void);

G_END_DECLS
