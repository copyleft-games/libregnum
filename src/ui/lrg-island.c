/* lrg-island.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgIsland: A pill-shaped panel that morphs between a compact and an expanded size.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-island.h"
#include "../lrg-log.h"

struct _LrgIsland
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgIsland, lrg_island, LRG_TYPE_CONTAINER)

static void
lrg_island_class_init (LrgIslandClass *klass)
{
}

static void
lrg_island_init (LrgIsland *self)
{
}

LrgIsland *
lrg_island_new (void)
{
	return g_object_new (LRG_TYPE_ISLAND, NULL);
}
