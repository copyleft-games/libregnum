/* lrg-skeleton2d-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkeleton2DComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-skeleton2d-component.h"

struct _LrgSkeleton2DComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgSkeleton2DComponent, lrg_skeleton2d_component, LRG_TYPE_COMPONENT)

static void
lrg_skeleton2d_component_class_init (LrgSkeleton2DComponentClass *klass)
{
}

static void
lrg_skeleton2d_component_init (LrgSkeleton2DComponent *self)
{
}

LrgSkeleton2DComponent *
lrg_skeleton2d_component_new (void)
{
	return g_object_new (LRG_TYPE_SKELETON2D_COMPONENT, NULL);
}
