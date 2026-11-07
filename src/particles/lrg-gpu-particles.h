/* lrg-gpu-particles.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGpuParticles: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_GPU_PARTICLES (lrg_gpu_particles_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGpuParticles, lrg_gpu_particles, LRG, GPU_PARTICLES, GObject)

/**
 * lrg_gpu_particles_new:
 *
 * Returns: (transfer full): a new #LrgGpuParticles
 */
LRG_AVAILABLE_IN_ALL
LrgGpuParticles * lrg_gpu_particles_new (void);

G_END_DECLS
