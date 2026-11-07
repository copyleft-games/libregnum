/* lrg-world3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorld3D: (stub, being written)
 */

#include "config.h"

#include "lrg-world3d.h"

struct _LrgWorld3D
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgWorld3D, lrg_world3d, G_TYPE_OBJECT)

static void
lrg_world3d_class_init (LrgWorld3DClass *klass)
{
}

static void
lrg_world3d_init (LrgWorld3D *self)
{
}

LrgWorld3D *
lrg_world3d_new (void)
{
	return g_object_new (LRG_TYPE_WORLD3D, NULL);
}
