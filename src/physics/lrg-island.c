/* lrg-island.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgIslandBuilder: (stub, being written)
 */

#include "config.h"

#include "lrg-island.h"

struct _LrgIslandBuilder
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgIslandBuilder, lrg_island_builder, G_TYPE_OBJECT)

static void
lrg_island_builder_class_init (LrgIslandBuilderClass *klass)
{
}

static void
lrg_island_builder_init (LrgIslandBuilder *self)
{
}

LrgIslandBuilder *
lrg_island_builder_new (void)
{
	return g_object_new (LRG_TYPE_ISLAND_BUILDER, NULL);
}
