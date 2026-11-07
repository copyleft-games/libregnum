/* lrg-volumetric-fog.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgVolumetricFog: (stub, being written)
 */

#include "config.h"

#include "lrg-volumetric-fog.h"

struct _LrgVolumetricFog
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgVolumetricFog, lrg_volumetric_fog, G_TYPE_OBJECT)

static void
lrg_volumetric_fog_class_init (LrgVolumetricFogClass *klass)
{
}

static void
lrg_volumetric_fog_init (LrgVolumetricFog *self)
{
}

LrgVolumetricFog *
lrg_volumetric_fog_new (void)
{
	return g_object_new (LRG_TYPE_VOLUMETRIC_FOG, NULL);
}
