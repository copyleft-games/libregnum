/* lrg-lod-group.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLodGroup: (stub, being written)
 */

#include "config.h"

#include "lrg-lod-group.h"

struct _LrgLodGroup
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgLodGroup, lrg_lod_group, G_TYPE_OBJECT)

static void
lrg_lod_group_class_init (LrgLodGroupClass *klass)
{
}

static void
lrg_lod_group_init (LrgLodGroup *self)
{
}

LrgLodGroup *
lrg_lod_group_new (void)
{
	return g_object_new (LRG_TYPE_LOD_GROUP, NULL);
}
