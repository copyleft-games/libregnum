/* lrg-particle-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgParticleComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-particle-component.h"

struct _LrgParticleComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgParticleComponent, lrg_particle_component, LRG_TYPE_COMPONENT)

static void
lrg_particle_component_class_init (LrgParticleComponentClass *klass)
{
}

static void
lrg_particle_component_init (LrgParticleComponent *self)
{
}

LrgParticleComponent *
lrg_particle_component_new (void)
{
	return g_object_new (LRG_TYPE_PARTICLE_COMPONENT, NULL);
}
