/* lrg-fracture.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFracture: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_FRACTURE (lrg_fracture_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFracture, lrg_fracture, LRG, FRACTURE, GObject)

/**
 * lrg_fracture_new:
 *
 * Returns: (transfer full): a new #LrgFracture
 */
LRG_AVAILABLE_IN_ALL
LrgFracture * lrg_fracture_new (void);

G_END_DECLS
