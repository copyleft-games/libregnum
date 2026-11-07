/* lrg-fracture.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFracture: (stub, being written)
 */

#include "config.h"

#include "lrg-fracture.h"

struct _LrgFracture
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgFracture, lrg_fracture, G_TYPE_OBJECT)

static void
lrg_fracture_class_init (LrgFractureClass *klass)
{
}

static void
lrg_fracture_init (LrgFracture *self)
{
}

LrgFracture *
lrg_fracture_new (void)
{
	return g_object_new (LRG_TYPE_FRACTURE, NULL);
}
