/* lrg-particle-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgParticleComponent: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../ecs/lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_PARTICLE_COMPONENT (lrg_particle_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleComponent, lrg_particle_component, LRG, PARTICLE_COMPONENT, LrgComponent)

/**
 * lrg_particle_component_new:
 *
 * Returns: (transfer full): a new #LrgParticleComponent
 */
LRG_AVAILABLE_IN_ALL
LrgParticleComponent * lrg_particle_component_new (void);

G_END_DECLS
