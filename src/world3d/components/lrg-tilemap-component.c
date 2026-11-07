/* lrg-tilemap-component.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTilemapComponent: (stub, being written)
 */

#include "config.h"

#include "lrg-tilemap-component.h"

struct _LrgTilemapComponent
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgTilemapComponent, lrg_tilemap_component, G_TYPE_OBJECT)

static void
lrg_tilemap_component_class_init (LrgTilemapComponentClass *klass)
{
}

static void
lrg_tilemap_component_init (LrgTilemapComponent *self)
{
}

LrgTilemapComponent *
lrg_tilemap_component_new (void)
{
	return g_object_new (LRG_TYPE_TILEMAP_COMPONENT, NULL);
}
