/* lrg-joint-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgJointComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-joint-component.h"

struct _LrgJointComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgJointComponent, lrg_joint_component, LRG_TYPE_COMPONENT)

static void
lrg_joint_component_class_init (LrgJointComponentClass *klass)
{
}

static void
lrg_joint_component_init (LrgJointComponent *self)
{
}

LrgJointComponent *
lrg_joint_component_new (void)
{
	return g_object_new (LRG_TYPE_JOINT_COMPONENT, NULL);
}
