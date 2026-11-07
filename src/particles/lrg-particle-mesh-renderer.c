/* lrg-particle-mesh-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgParticleMeshRenderer: (stub, being written)
 */

#include "config.h"

#include "lrg-particle-mesh-renderer.h"

struct _LrgParticleMeshRenderer
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgParticleMeshRenderer, lrg_particle_mesh_renderer, G_TYPE_OBJECT)

static void
lrg_particle_mesh_renderer_class_init (LrgParticleMeshRendererClass *klass)
{
}

static void
lrg_particle_mesh_renderer_init (LrgParticleMeshRenderer *self)
{
}

LrgParticleMeshRenderer *
lrg_particle_mesh_renderer_new (void)
{
	return g_object_new (LRG_TYPE_PARTICLE_MESH_RENDERER, NULL);
}
