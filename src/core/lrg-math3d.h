/* lrg-math3d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMath3d: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_MATH3D (lrg_math3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMath3d, lrg_math3d, LRG, MATH3D, GObject)

/**
 * lrg_math3d_new:
 *
 * Returns: (transfer full): a new #LrgMath3d
 */
LRG_AVAILABLE_IN_ALL
LrgMath3d * lrg_math3d_new (void);

G_END_DECLS
