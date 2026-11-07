/* lrg-particle-ribbon-renderer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgParticleRibbonRenderer: (stub, being written)
 */

#include "config.h"

#include "lrg-particle-ribbon-renderer.h"

struct _LrgParticleRibbonRenderer
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgParticleRibbonRenderer, lrg_particle_ribbon_renderer, G_TYPE_OBJECT)

static void
lrg_particle_ribbon_renderer_class_init (LrgParticleRibbonRendererClass *klass)
{
}

static void
lrg_particle_ribbon_renderer_init (LrgParticleRibbonRenderer *self)
{
}

LrgParticleRibbonRenderer *
lrg_particle_ribbon_renderer_new (void)
{
	return g_object_new (LRG_TYPE_PARTICLE_RIBBON_RENDERER, NULL);
}
