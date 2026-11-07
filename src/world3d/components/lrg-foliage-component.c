/* lrg-foliage-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFoliageComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-foliage-component.h"

struct _LrgFoliageComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgFoliageComponent, lrg_foliage_component, LRG_TYPE_COMPONENT)

static void
lrg_foliage_component_class_init (LrgFoliageComponentClass *klass)
{
}

static void
lrg_foliage_component_init (LrgFoliageComponent *self)
{
}

LrgFoliageComponent *
lrg_foliage_component_new (void)
{
	return g_object_new (LRG_TYPE_FOLIAGE_COMPONENT, NULL);
}
