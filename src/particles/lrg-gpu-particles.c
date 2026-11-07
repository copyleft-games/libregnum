/* lrg-gpu-particles.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGpuParticles: (stub, being written)
 */

#include "config.h"

#include "lrg-gpu-particles.h"

struct _LrgGpuParticles
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgGpuParticles, lrg_gpu_particles, G_TYPE_OBJECT)

static void
lrg_gpu_particles_class_init (LrgGpuParticlesClass *klass)
{
}

static void
lrg_gpu_particles_init (LrgGpuParticles *self)
{
}

LrgGpuParticles *
lrg_gpu_particles_new (void)
{
	return g_object_new (LRG_TYPE_GPU_PARTICLES, NULL);
}
