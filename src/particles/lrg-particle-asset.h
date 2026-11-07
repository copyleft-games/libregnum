/* lrg-particle-asset.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgParticleAsset: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_PARTICLE_ASSET (lrg_particle_asset_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgParticleAsset, lrg_particle_asset, LRG, PARTICLE_ASSET, GObject)

/**
 * lrg_particle_asset_new:
 *
 * Returns: (transfer full): a new #LrgParticleAsset
 */
LRG_AVAILABLE_IN_ALL
LrgParticleAsset * lrg_particle_asset_new (void);

G_END_DECLS
