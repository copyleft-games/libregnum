/* lrg-terrain-brush.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainBrush: (stub, being written)
 */

#include "config.h"

#include "lrg-terrain-brush.h"

struct _LrgTerrainBrush
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgTerrainBrush, lrg_terrain_brush, G_TYPE_OBJECT)

static void
lrg_terrain_brush_class_init (LrgTerrainBrushClass *klass)
{
}

static void
lrg_terrain_brush_init (LrgTerrainBrush *self)
{
}

LrgTerrainBrush *
lrg_terrain_brush_new (void)
{
	return g_object_new (LRG_TYPE_TERRAIN_BRUSH, NULL);
}
