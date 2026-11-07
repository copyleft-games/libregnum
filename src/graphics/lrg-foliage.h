/* lrg-foliage.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFoliage: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_FOLIAGE (lrg_foliage_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFoliage, lrg_foliage, LRG, FOLIAGE, GObject)

/**
 * lrg_foliage_new:
 *
 * Returns: (transfer full): a new #LrgFoliage
 */
LRG_AVAILABLE_IN_ALL
LrgFoliage * lrg_foliage_new (void);

G_END_DECLS
