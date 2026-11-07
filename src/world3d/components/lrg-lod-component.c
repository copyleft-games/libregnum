/* lrg-lod-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLodComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-lod-component.h"

struct _LrgLodComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgLodComponent, lrg_lod_component, LRG_TYPE_COMPONENT)

static void
lrg_lod_component_class_init (LrgLodComponentClass *klass)
{
}

static void
lrg_lod_component_init (LrgLodComponent *self)
{
}

LrgLodComponent *
lrg_lod_component_new (void)
{
	return g_object_new (LRG_TYPE_LOD_COMPONENT, NULL);
}
