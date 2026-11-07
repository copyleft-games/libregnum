/* lrg-lightmap-baker.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightmapBaker: (stub, being written)
 */

#include "config.h"

#include "lrg-lightmap-baker.h"

struct _LrgLightmapBaker
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgLightmapBaker, lrg_lightmap_baker, G_TYPE_OBJECT)

static void
lrg_lightmap_baker_class_init (LrgLightmapBakerClass *klass)
{
}

static void
lrg_lightmap_baker_init (LrgLightmapBaker *self)
{
}

LrgLightmapBaker *
lrg_lightmap_baker_new (void)
{
	return g_object_new (LRG_TYPE_LIGHTMAP_BAKER, NULL);
}
