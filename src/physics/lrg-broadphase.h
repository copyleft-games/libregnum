/* lrg-broadphase.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBroadphase: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_BROADPHASE (lrg_broadphase_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBroadphase, lrg_broadphase, LRG, BROADPHASE, GObject)

/**
 * lrg_broadphase_new:
 *
 * Returns: (transfer full): a new #LrgBroadphase
 */
LRG_AVAILABLE_IN_ALL
LrgBroadphase * lrg_broadphase_new (void);

G_END_DECLS
