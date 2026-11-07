/* lrg-water-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWaterComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-water-component.h"

struct _LrgWaterComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgWaterComponent, lrg_water_component, LRG_TYPE_COMPONENT)

static void
lrg_water_component_class_init (LrgWaterComponentClass *klass)
{
}

static void
lrg_water_component_init (LrgWaterComponent *self)
{
}

LrgWaterComponent *
lrg_water_component_new (void)
{
	return g_object_new (LRG_TYPE_WATER_COMPONENT, NULL);
}
