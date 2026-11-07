/* lrg-bake-bvh.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBakeBvh: (stub, being written)
 */

#include "config.h"

#include "lrg-bake-bvh.h"

struct _LrgBakeBvh
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgBakeBvh, lrg_bake_bvh, G_TYPE_OBJECT)

static void
lrg_bake_bvh_class_init (LrgBakeBvhClass *klass)
{
}

static void
lrg_bake_bvh_init (LrgBakeBvh *self)
{
}

LrgBakeBvh *
lrg_bake_bvh_new (void)
{
	return g_object_new (LRG_TYPE_BAKE_BVH, NULL);
}
