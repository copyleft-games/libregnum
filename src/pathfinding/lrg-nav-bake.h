/* lrg-nav-bake.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNavBake: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_NAV_BAKE (lrg_nav_bake_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNavBake, lrg_nav_bake, LRG, NAV_BAKE, GObject)

/**
 * lrg_nav_bake_new:
 *
 * Returns: (transfer full): a new #LrgNavBake
 */
LRG_AVAILABLE_IN_ALL
LrgNavBake * lrg_nav_bake_new (void);

G_END_DECLS
