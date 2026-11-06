/* lrg-tile-view.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTileView: A virtualized grid of tiles (icons, thumbnails).
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-tile-view.h"
#include "../lrg-log.h"

struct _LrgTileView
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgTileView, lrg_tile_view, LRG_TYPE_CONTAINER)

static void
lrg_tile_view_class_init (LrgTileViewClass *klass)
{
}

static void
lrg_tile_view_init (LrgTileView *self)
{
}

LrgTileView *
lrg_tile_view_new (void)
{
	return g_object_new (LRG_TYPE_TILE_VIEW, NULL);
}
