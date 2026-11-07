/* lrg-terrain-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-terrain-component.h"

struct _LrgTerrainComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgTerrainComponent, lrg_terrain_component, LRG_TYPE_COMPONENT)

static void
lrg_terrain_component_class_init (LrgTerrainComponentClass *klass)
{
}

static void
lrg_terrain_component_init (LrgTerrainComponent *self)
{
}

LrgTerrainComponent *
lrg_terrain_component_new (void)
{
	return g_object_new (LRG_TYPE_TERRAIN_COMPONENT, NULL);
}
