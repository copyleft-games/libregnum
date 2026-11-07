/* lrg-decal-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgDecalComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-decal-component.h"

struct _LrgDecalComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgDecalComponent, lrg_decal_component, LRG_TYPE_COMPONENT)

static void
lrg_decal_component_class_init (LrgDecalComponentClass *klass)
{
}

static void
lrg_decal_component_init (LrgDecalComponent *self)
{
}

LrgDecalComponent *
lrg_decal_component_new (void)
{
	return g_object_new (LRG_TYPE_DECAL_COMPONENT, NULL);
}
