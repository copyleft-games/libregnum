/* lrg-foliage.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFoliage: (stub, being written)
 */

#include "config.h"

#include "lrg-foliage.h"

struct _LrgFoliage
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgFoliage, lrg_foliage, G_TYPE_OBJECT)

static void
lrg_foliage_class_init (LrgFoliageClass *klass)
{
}

static void
lrg_foliage_init (LrgFoliage *self)
{
}

LrgFoliage *
lrg_foliage_new (void)
{
	return g_object_new (LRG_TYPE_FOLIAGE, NULL);
}
