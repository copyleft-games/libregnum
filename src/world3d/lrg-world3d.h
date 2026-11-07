/* lrg-world3d.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorld3D: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_WORLD3D (lrg_world3d_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWorld3D, lrg_world3d, LRG, WORLD3D, GObject)

/**
 * lrg_world3d_new:
 *
 * Returns: (transfer full): a new #LrgWorld3D
 */
LRG_AVAILABLE_IN_ALL
LrgWorld3D * lrg_world3d_new (void);

G_END_DECLS
