/* lrg-entity3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgEntity3D: (stub, being written)
 */

#include "config.h"

#include "lrg-entity3d.h"

struct _LrgEntity3D
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgEntity3D, lrg_entity3d, G_TYPE_OBJECT)

static void
lrg_entity3d_class_init (LrgEntity3DClass *klass)
{
}

static void
lrg_entity3d_init (LrgEntity3D *self)
{
}

LrgEntity3D *
lrg_entity3d_new (void)
{
	return g_object_new (LRG_TYPE_ENTITY3D, NULL);
}
