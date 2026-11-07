/* lrg-terrain-brush.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainBrush: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_TERRAIN_BRUSH (lrg_terrain_brush_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTerrainBrush, lrg_terrain_brush, LRG, TERRAIN_BRUSH, GObject)

/**
 * lrg_terrain_brush_new:
 *
 * Returns: (transfer full): a new #LrgTerrainBrush
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainBrush * lrg_terrain_brush_new (void);

G_END_DECLS
