/* lrg-collide.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCollide: (stub, being written)
 */

#include "config.h"

#include "lrg-collide.h"

struct _LrgCollide
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgCollide, lrg_collide, G_TYPE_OBJECT)

static void
lrg_collide_class_init (LrgCollideClass *klass)
{
}

static void
lrg_collide_init (LrgCollide *self)
{
}

LrgCollide *
lrg_collide_new (void)
{
	return g_object_new (LRG_TYPE_COLLIDE, NULL);
}
