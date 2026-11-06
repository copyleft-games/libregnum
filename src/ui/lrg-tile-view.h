/* lrg-tile-view.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTileView: A virtualized grid of tiles (icons, thumbnails).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_TILE_VIEW (lrg_tile_view_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTileView, lrg_tile_view, LRG, TILE_VIEW, LrgContainer)

/**
 * lrg_tile_view_new:
 *
 * Returns: (transfer full): a new #LrgTileView
 */
LRG_AVAILABLE_IN_ALL
LrgTileView * lrg_tile_view_new (void);

G_END_DECLS
