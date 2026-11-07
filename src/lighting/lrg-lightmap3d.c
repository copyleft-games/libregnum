/* lrg-lightmap3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLightmap3D: (stub, being written)
 */

#include "config.h"

#include "lrg-lightmap3d.h"

struct _LrgLightmap3D
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgLightmap3D, lrg_lightmap3d, G_TYPE_OBJECT)

static void
lrg_lightmap3d_class_init (LrgLightmap3DClass *klass)
{
}

static void
lrg_lightmap3d_init (LrgLightmap3D *self)
{
}

LrgLightmap3D *
lrg_lightmap3d_new (void)
{
	return g_object_new (LRG_TYPE_LIGHTMAP3D, NULL);
}
