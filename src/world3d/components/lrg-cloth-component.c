/* lrg-cloth-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgClothComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-cloth-component.h"

struct _LrgClothComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgClothComponent, lrg_cloth_component, LRG_TYPE_COMPONENT)

static void
lrg_cloth_component_class_init (LrgClothComponentClass *klass)
{
}

static void
lrg_cloth_component_init (LrgClothComponent *self)
{
}

LrgClothComponent *
lrg_cloth_component_new (void)
{
	return g_object_new (LRG_TYPE_CLOTH_COMPONENT, NULL);
}
