/* lrg-tilemap-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTilemapComponent: a tile grid in the entity's local XY plane (x right,
 * rows going down -Y, layers stacked toward +Z), drawn like the editor's
 * TILEMAP visual through one %LRG_RENDER_ITEM_CUSTOM item per visible
 * layer.  The cells come from an #LrgNodeTilemap (the level data) or from
 * a runtime #LrgTilemap; the image from an #LrgTileset.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../lrg-world3d.h"

G_BEGIN_DECLS

/**
 * LRG_TILEMAP_COMPONENT_LAYER_GAP:
 *
 * Local Z distance between stacked tile layers (layer N is drawn at
 * N times this), matching the editor viewport.
 */
#define LRG_TILEMAP_COMPONENT_LAYER_GAP (0.002f)

#define LRG_TYPE_TILEMAP_COMPONENT (lrg_tilemap_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTilemapComponent, lrg_tilemap_component, LRG, TILEMAP_COMPONENT, LrgComponent)

/**
 * lrg_tilemap_component_new:
 *
 * Creates an empty tilemap component (tile size 1 world unit).
 *
 * Returns: (transfer full): a new #LrgTilemapComponent
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapComponent * lrg_tilemap_component_new (void);

/**
 * lrg_tilemap_component_new_from_node_tilemap:
 * @node_tilemap: the cells (copied)
 * @tile_size: world units per tile (> 0)
 *
 * Creates a component from level tile data, as
 * lrg_level_instantiate_3d() does for TILEMAP nodes.
 *
 * Returns: (transfer full): a new #LrgTilemapComponent
 */
LRG_AVAILABLE_IN_ALL
LrgTilemapComponent * lrg_tilemap_component_new_from_node_tilemap (LrgNodeTilemap *node_tilemap,
                                                                   gfloat          tile_size);

/**
 * lrg_tilemap_component_get_node_tilemap:
 * @self: an #LrgTilemapComponent
 *
 * Returns: (transfer none) (nullable): the level tile data, if any
 */
LRG_AVAILABLE_IN_ALL
LrgNodeTilemap * lrg_tilemap_component_get_node_tilemap (LrgTilemapComponent *self);

/**
 * lrg_tilemap_component_set_node_tilemap:
 * @self: an #LrgTilemapComponent
 * @node_tilemap: (nullable): level tile data (referenced, not copied:
 *   edits to it show on the next frame)
 *
 * Draws the cells of @node_tilemap.  Wins over a runtime tilemap set with
 * lrg_tilemap_component_set_tilemap().
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_component_set_node_tilemap (LrgTilemapComponent *self,
                                             LrgNodeTilemap      *node_tilemap);

/**
 * lrg_tilemap_component_get_tilemap:
 * @self: an #LrgTilemapComponent
 *
 * Gets the runtime tilemap: the one set with
 * lrg_tilemap_component_set_tilemap(), or one built from the node tilemap
 * with lrg_node_tilemap_to_runtime() once a tileset is available (cached
 * until the cells source or the tileset changes).
 *
 * Returns: (transfer none) (nullable): the runtime tilemap, %NULL without
 *   cells or without a tileset
 */
LRG_AVAILABLE_IN_ALL
LrgTilemap * lrg_tilemap_component_get_tilemap (LrgTilemapComponent *self);

/**
 * lrg_tilemap_component_set_tilemap:
 * @self: an #LrgTilemapComponent
 * @tilemap: (nullable): a runtime tilemap (referenced)
 *
 * Draws a runtime tilemap (for instance one a 2D game loaded); its tileset
 * becomes the component's tileset when none is set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_component_set_tilemap (LrgTilemapComponent *self,
                                        LrgTilemap          *tilemap);

/**
 * lrg_tilemap_component_get_tileset:
 * @self: an #LrgTilemapComponent
 *
 * Returns: (transfer none) (nullable): the tileset, explicit or loaded from
 *   #LrgTilemapComponent:tileset-path
 */
LRG_AVAILABLE_IN_ALL
LrgTileset * lrg_tilemap_component_get_tileset (LrgTilemapComponent *self);

/**
 * lrg_tilemap_component_set_tileset:
 * @self: an #LrgTilemapComponent
 * @tileset: (nullable): a tileset (referenced)
 *
 * Sets the tile images.  Without a tileset every tile id draws as a flat
 * colour swatch, as in the editor.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_component_set_tileset (LrgTilemapComponent *self,
                                        LrgTileset          *tileset);

/**
 * lrg_tilemap_component_get_tileset_path:
 * @self: an #LrgTilemapComponent
 *
 * Returns: (transfer none) (nullable): the tileset image path or GUID
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_tilemap_component_get_tileset_path (LrgTilemapComponent *self);

/**
 * lrg_tilemap_component_set_tileset_path:
 * @self: an #LrgTilemapComponent
 * @path: (nullable): the tileset image (the editor TILEMAP visual's asset)
 *
 * Sets the tileset image, loaded on the next collect_render() through
 * #LrgTilemapComponent:asset-manager (or from the file when a window is
 * open) and sliced by the node tilemap's tile pixel size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_component_set_tileset_path (LrgTilemapComponent *self,
                                             const gchar         *path);

/**
 * lrg_tilemap_component_set_asset_manager:
 * @self: an #LrgTilemapComponent
 * @manager: (nullable): an asset manager
 *
 * Sets the asset manager the tileset path loads through.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_component_set_asset_manager (LrgTilemapComponent *self,
                                              LrgAssetManager     *manager);

/**
 * lrg_tilemap_component_get_tile_size:
 * @self: an #LrgTilemapComponent
 *
 * Returns: world units per tile
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tilemap_component_get_tile_size (LrgTilemapComponent *self);

/**
 * lrg_tilemap_component_set_tile_size:
 * @self: an #LrgTilemapComponent
 * @tile_size: world units per tile (> 0)
 *
 * Sets the tile size (the editor TILEMAP visual's tile_size param).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_component_set_tile_size (LrgTilemapComponent *self,
                                          gfloat               tile_size);

/**
 * lrg_tilemap_component_get_grid_size:
 * @self: an #LrgTilemapComponent
 * @out_width: (out) (optional): columns
 * @out_height: (out) (optional): rows
 *
 * Gets the grid size of the cell source (0 x 0 without one).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tilemap_component_get_grid_size (LrgTilemapComponent *self,
                                          guint               *out_width,
                                          guint               *out_height);

/**
 * lrg_tilemap_component_get_n_layers:
 * @self: an #LrgTilemapComponent
 *
 * Returns: the number of tile layers of the cell source
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tilemap_component_get_n_layers (LrgTilemapComponent *self);

/**
 * lrg_tilemap_component_get_layer_cells:
 * @self: an #LrgTilemapComponent
 * @layer: a tile layer index
 * @out_visible: (out) (optional): whether the layer is visible
 *
 * Gets a layer's cells, row major, width x height tile ids (0 is empty,
 * id N draws tileset tile N - 1).
 *
 * Returns: (transfer none) (nullable) (array): the cells, %NULL when
 *   @layer is out of range
 */
LRG_AVAILABLE_IN_ALL
const guint * lrg_tilemap_component_get_layer_cells (LrgTilemapComponent *self,
                                                     guint                layer,
                                                     gboolean            *out_visible);

/**
 * lrg_tilemap_component_get_local_bounds:
 * @self: an #LrgTilemapComponent
 * @out: (out caller-allocates): the grid's bounds in local space
 *
 * The grid spans x 0 .. width * tile size, y -height * tile size .. 0 and
 * z 0 .. (layers - 1) * %LRG_TILEMAP_COMPONENT_LAYER_GAP.
 *
 * Returns: %TRUE if the component has cells
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tilemap_component_get_local_bounds (LrgTilemapComponent *self,
                                                 LrgBoundingBox3D    *out);

G_END_DECLS
