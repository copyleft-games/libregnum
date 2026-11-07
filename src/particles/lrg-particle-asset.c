/* lrg-particle-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgParticleAsset: (stub, being written)
 */

#include "config.h"

#include "lrg-particle-asset.h"

struct _LrgParticleAsset
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgParticleAsset, lrg_particle_asset, G_TYPE_OBJECT)

static void
lrg_particle_asset_class_init (LrgParticleAssetClass *klass)
{
}

static void
lrg_particle_asset_init (LrgParticleAsset *self)
{
}

LrgParticleAsset *
lrg_particle_asset_new (void)
{
	return g_object_new (LRG_TYPE_PARTICLE_ASSET, NULL);
}
