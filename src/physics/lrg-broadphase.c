/* lrg-broadphase.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBroadphase: (stub, being written)
 */

#include "config.h"

#include "lrg-broadphase.h"

struct _LrgBroadphase
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgBroadphase, lrg_broadphase, G_TYPE_OBJECT)

static void
lrg_broadphase_class_init (LrgBroadphaseClass *klass)
{
}

static void
lrg_broadphase_init (LrgBroadphase *self)
{
}

LrgBroadphase *
lrg_broadphase_new (void)
{
	return g_object_new (LRG_TYPE_BROADPHASE, NULL);
}
