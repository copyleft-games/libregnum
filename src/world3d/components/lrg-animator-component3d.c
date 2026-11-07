/* lrg-animator-component3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnimatorComponent3D: (stub, being written)
 */

#include "config.h"

#include "lrg-animator-component3d.h"

struct _LrgAnimatorComponent3D
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgAnimatorComponent3D, lrg_animator_component3d, LRG_TYPE_COMPONENT)

static void
lrg_animator_component3d_class_init (LrgAnimatorComponent3DClass *klass)
{
}

static void
lrg_animator_component3d_init (LrgAnimatorComponent3D *self)
{
}

LrgAnimatorComponent3D *
lrg_animator_component3d_new (void)
{
	return g_object_new (LRG_TYPE_ANIMATOR_COMPONENT3D, NULL);
}
