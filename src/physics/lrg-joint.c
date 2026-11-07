/* lrg-joint.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgJoint: (stub, being written)
 */

#include "config.h"

#include "lrg-joint.h"

struct _LrgJoint
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgJoint, lrg_joint, G_TYPE_OBJECT)

static void
lrg_joint_class_init (LrgJointClass *klass)
{
}

static void
lrg_joint_init (LrgJoint *self)
{
}

LrgJoint *
lrg_joint_new (void)
{
	return g_object_new (LRG_TYPE_JOINT, NULL);
}
