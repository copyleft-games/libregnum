/* lrg-water-body.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaterBody: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_WATER_BODY (lrg_water_body_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWaterBody, lrg_water_body, LRG, WATER_BODY, GObject)

/**
 * lrg_water_body_new:
 *
 * Returns: (transfer full): a new #LrgWaterBody
 */
LRG_AVAILABLE_IN_ALL
LrgWaterBody * lrg_water_body_new (void);

G_END_DECLS
