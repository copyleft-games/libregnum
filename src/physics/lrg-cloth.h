/* lrg-cloth.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCloth: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_CLOTH (lrg_cloth_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCloth, lrg_cloth, LRG, CLOTH, GObject)

/**
 * lrg_cloth_new:
 *
 * Returns: (transfer full): a new #LrgCloth
 */
LRG_AVAILABLE_IN_ALL
LrgCloth * lrg_cloth_new (void);

G_END_DECLS
