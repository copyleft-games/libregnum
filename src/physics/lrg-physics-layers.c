/* lrg-physics-layers.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhysicsLayers: (stub, being written)
 */

#include "config.h"

#include "lrg-physics-layers.h"

struct _LrgPhysicsLayers
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgPhysicsLayers, lrg_physics_layers, G_TYPE_OBJECT)

static void
lrg_physics_layers_class_init (LrgPhysicsLayersClass *klass)
{
}

static void
lrg_physics_layers_init (LrgPhysicsLayers *self)
{
}

LrgPhysicsLayers *
lrg_physics_layers_new (void)
{
	return g_object_new (LRG_TYPE_PHYSICS_LAYERS, NULL);
}
