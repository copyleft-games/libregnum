/* lrg-rigid-body-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRigidBodyComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-rigid-body-component.h"

struct _LrgRigidBodyComponent
{
	LrgComponent parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgRigidBodyComponent, lrg_rigid_body_component, LRG_TYPE_COMPONENT)

static void
lrg_rigid_body_component_class_init (LrgRigidBodyComponentClass *klass)
{
}

static void
lrg_rigid_body_component_init (LrgRigidBodyComponent *self)
{
}

LrgRigidBodyComponent *
lrg_rigid_body_component_new (void)
{
	return g_object_new (LRG_TYPE_RIGID_BODY_COMPONENT, NULL);
}
