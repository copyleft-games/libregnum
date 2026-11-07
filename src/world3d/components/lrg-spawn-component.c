/* lrg-spawn-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpawnComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-spawn-component.h"

struct _LrgSpawnComponent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSpawnComponent, lrg_spawn_component, G_TYPE_OBJECT)

static void
lrg_spawn_component_class_init (LrgSpawnComponentClass *klass)
{
}

static void
lrg_spawn_component_init (LrgSpawnComponent *self)
{
}

LrgSpawnComponent *
lrg_spawn_component_new (void)
{
	return g_object_new (LRG_TYPE_SPAWN_COMPONENT, NULL);
}
