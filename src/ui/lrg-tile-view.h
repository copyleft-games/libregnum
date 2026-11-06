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
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_TILE_VIEW (lrg_tile_view_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTileView, lrg_tile_view, LRG, TILE_VIEW, LrgContainer)

/**
 * LRG_TILE_VIEW_INVALID_POSITION:
 *
 * Returned by lrg_tile_view_get_selected() and
 * lrg_tile_view_get_cursor() when there is no such tile.
 */
#define LRG_TILE_VIEW_INVALID_POSITION (G_MAXUINT)

/**
 * LrgTileViewCreateFunc:
 * @self: the tile view
 * @user_data: data given to lrg_tile_view_set_factory()
 *
 * Creates an empty tile widget. Tiles are recycled.
 *
 * Returns: (transfer full): a new tile widget
 */
typedef LrgWidget * (*LrgTileViewCreateFunc) (LrgTileView *self,
                                              gpointer     user_data);

/**
 * LrgTileViewBindFunc:
 * @self: the tile view
 * @tile: a tile made by the #LrgTileViewCreateFunc
 * @item: the model item to show
 * @position: index of @item in the model
 * @user_data: data given to lrg_tile_view_set_factory()
 *
 * Fills @tile with the contents of @item.
 */
typedef void (*LrgTileViewBindFunc) (LrgTileView *self,
                                     LrgWidget   *tile,
                                     GObject     *item,
                                     guint        position,
                                     gpointer     user_data);

/**
 * lrg_tile_view_new:
 *
 * Creates an empty tile view with 96 x 96 tiles, 8 units apart, the
 * default factory (a centred label with the item's "string", "text",
 * "name", "label" or "title" property) and single selection.
 *
 * Returns: (transfer full): a new #LrgTileView
 */
LRG_AVAILABLE_IN_ALL
LrgTileView * lrg_tile_view_new (void);

/**
 * lrg_tile_view_set_model:
 * @self: an #LrgTileView
 * @model: (nullable): the items to show
 *
 * Shows @model, one tile per item; the selection is cleared.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_view_set_model (LrgTileView *self,
                              GListModel  *model);

/**
 * lrg_tile_view_get_model:
 * @self: an #LrgTileView
 *
 * Returns: (transfer none) (nullable): the model
 */
LRG_AVAILABLE_IN_ALL
GListModel * lrg_tile_view_get_model (LrgTileView *self);

/**
 * lrg_tile_view_set_factory:
 * @self: an #LrgTileView
 * @create: (nullable) (scope notified) (closure user_data): makes tile
 *   widgets, or %NULL for labels
 * @bind: (nullable) (scope notified) (closure user_data): fills a
 *   tile, or %NULL for the default
 * @user_data: data for both functions
 * @destroy: (nullable): frees @user_data when replaced
 *
 * Sets how tiles are made. Existing tiles are rebuilt.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_view_set_factory (LrgTileView           *self,
                                LrgTileViewCreateFunc  create,
                                LrgTileViewBindFunc    bind,
                                gpointer               user_data,
                                GDestroyNotify         destroy);

/**
 * lrg_tile_view_set_tile_size:
 * @self: an #LrgTileView
 * @width: tile width (at least 8)
 * @height: tile height (at least 8)
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_view_set_tile_size (LrgTileView *self,
                                  gfloat       width,
                                  gfloat       height);

/**
 * lrg_tile_view_get_tile_size:
 * @self: an #LrgTileView
 * @width: (out) (optional): tile width
 * @height: (out) (optional): tile height
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_view_get_tile_size (LrgTileView *self,
                                  gfloat      *width,
                                  gfloat      *height);

/**
 * lrg_tile_view_get_tile_spacing:
 * @self: an #LrgTileView
 *
 * Returns: gap between tiles, both ways
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tile_view_get_tile_spacing (LrgTileView *self);

/**
 * lrg_tile_view_set_tile_spacing:
 * @self: an #LrgTileView
 * @spacing: gap between tiles, both ways
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_view_set_tile_spacing (LrgTileView *self,
                                     gfloat       spacing);

/**
 * lrg_tile_view_get_columns:
 * @self: an #LrgTileView
 *
 * Returns: tiles per row for the current width (at least 1)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_view_get_columns (LrgTileView *self);

/**
 * lrg_tile_view_get_selection_mode:
 * @self: an #LrgTileView
 *
 * Returns: how many tiles can be selected
 */
LRG_AVAILABLE_IN_ALL
LrgSelectionMode lrg_tile_view_get_selection_mode (LrgTileView *self);

/**
 * lrg_tile_view_set_selection_mode:
 * @self: an #LrgTileView
 * @mode: none, single (the default) or multiple (Ctrl click)
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_view_set_selection_mode (LrgTileView      *self,
                                       LrgSelectionMode  mode);

/**
 * lrg_tile_view_select:
 * @self: an #LrgTileView
 * @position: a model position
 *
 * Selects the tile and moves the cursor there.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_view_select (LrgTileView *self,
                           guint        position);

/**
 * lrg_tile_view_unselect_all:
 * @self: an #LrgTileView
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_view_unselect_all (LrgTileView *self);

/**
 * lrg_tile_view_get_selected:
 * @self: an #LrgTileView
 *
 * Returns: the cursor tile if selected, else the lowest selected tile,
 *   or %LRG_TILE_VIEW_INVALID_POSITION
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_view_get_selected (LrgTileView *self);

/**
 * lrg_tile_view_is_selected:
 * @self: an #LrgTileView
 * @position: a model position
 *
 * Returns: %TRUE if the tile is selected
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tile_view_is_selected (LrgTileView *self,
                                    guint        position);

/**
 * lrg_tile_view_get_n_selected:
 * @self: an #LrgTileView
 *
 * Returns: number of selected tiles
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_view_get_n_selected (LrgTileView *self);

/**
 * lrg_tile_view_get_cursor:
 * @self: an #LrgTileView
 *
 * Returns: the keyboard cursor tile, or %LRG_TILE_VIEW_INVALID_POSITION
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_view_get_cursor (LrgTileView *self);

/**
 * lrg_tile_view_scroll_to:
 * @self: an #LrgTileView
 * @position: a model position
 * @animate: glide instead of jumping
 *
 * Scrolls by the smallest amount that shows the whole tile row.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tile_view_scroll_to (LrgTileView *self,
                              guint        position,
                              gboolean     animate);

/**
 * lrg_tile_view_get_scroll_offset:
 * @self: an #LrgTileView
 *
 * Returns: the vertical offset currently displayed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tile_view_get_scroll_offset (LrgTileView *self);

/**
 * lrg_tile_view_get_tile_widget:
 * @self: an #LrgTileView
 * @position: a model position
 *
 * Returns: (transfer none) (nullable): the realized tile widget, or
 *   %NULL when the tile is scrolled away
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_tile_view_get_tile_widget (LrgTileView *self,
                                           guint        position);

/**
 * lrg_tile_view_get_n_tiles:
 * @self: an #LrgTileView
 *
 * Returns: number of realized tile widgets, independent of the model
 *   size
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tile_view_get_n_tiles (LrgTileView *self);

G_END_DECLS
