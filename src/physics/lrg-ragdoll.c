/* lrg-ragdoll.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRagdoll: (stub, being written)
 */

#include "config.h"

#include "lrg-ragdoll.h"

struct _LrgRagdoll
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgRagdoll, lrg_ragdoll, G_TYPE_OBJECT)

static void
lrg_ragdoll_class_init (LrgRagdollClass *klass)
{
}

static void
lrg_ragdoll_init (LrgRagdoll *self)
{
}

LrgRagdoll *
lrg_ragdoll_new (void)
{
	return g_object_new (LRG_TYPE_RAGDOLL, NULL);
}
