/* lrg-physics-shape.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPhysicsShape: (stub, being written)
 */

#include "config.h"

#include "lrg-physics-shape.h"

struct _LrgPhysicsShape
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgPhysicsShape, lrg_physics_shape, G_TYPE_OBJECT)

static void
lrg_physics_shape_class_init (LrgPhysicsShapeClass *klass)
{
}

static void
lrg_physics_shape_init (LrgPhysicsShape *self)
{
}

LrgPhysicsShape *
lrg_physics_shape_new (void)
{
	return g_object_new (LRG_TYPE_PHYSICS_SHAPE, NULL);
}
