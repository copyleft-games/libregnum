/* lrg-skeleton2d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkeleton2D: (stub, being written)
 */

#include "config.h"

#include "lrg-skeleton2d.h"

struct _LrgSkeleton2D
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSkeleton2D, lrg_skeleton2d, G_TYPE_OBJECT)

static void
lrg_skeleton2d_class_init (LrgSkeleton2DClass *klass)
{
}

static void
lrg_skeleton2d_init (LrgSkeleton2D *self)
{
}

LrgSkeleton2D *
lrg_skeleton2d_new (void)
{
	return g_object_new (LRG_TYPE_SKELETON2D, NULL);
}
