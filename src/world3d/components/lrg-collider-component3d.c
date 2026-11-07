/* lrg-collider-component3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgColliderComponent3D: (stub, being written)
 */

#include "config.h"

#include "lrg-collider-component3d.h"

struct _LrgColliderComponent3D
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgColliderComponent3D, lrg_collider_component3d, LRG_TYPE_COMPONENT)

static void
lrg_collider_component3d_class_init (LrgColliderComponent3DClass *klass)
{
}

static void
lrg_collider_component3d_init (LrgColliderComponent3D *self)
{
}

LrgColliderComponent3D *
lrg_collider_component3d_new (void)
{
	return g_object_new (LRG_TYPE_COLLIDER_COMPONENT3D, NULL);
}
