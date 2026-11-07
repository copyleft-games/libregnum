/* lrg-cloth.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCloth: (stub, being written)
 */

#include "config.h"

#include "lrg-cloth.h"

struct _LrgCloth
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgCloth, lrg_cloth, G_TYPE_OBJECT)

static void
lrg_cloth_class_init (LrgClothClass *klass)
{
}

static void
lrg_cloth_init (LrgCloth *self)
{
}

LrgCloth *
lrg_cloth_new (void)
{
	return g_object_new (LRG_TYPE_CLOTH, NULL);
}
