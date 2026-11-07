/* lrg-math3d.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgMath3d: (stub, being written)
 */

#include "config.h"

#include "lrg-math3d.h"

struct _LrgMath3d
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgMath3d, lrg_math3d, G_TYPE_OBJECT)

static void
lrg_math3d_class_init (LrgMath3dClass *klass)
{
}

static void
lrg_math3d_init (LrgMath3d *self)
{
}

LrgMath3d *
lrg_math3d_new (void)
{
	return g_object_new (LRG_TYPE_MATH3D, NULL);
}
