/* lrg-foliage-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgFoliageComponent: draws an #LrgFoliage on an entity.
 *
 * A terrain's own foliage is drawn by its #LrgTerrainComponent; this
 * component is for foliage sets that live apart from a terrain asset
 * (scattered over another terrain asset, or filled by game code), e.g. a
 * seasonal layer toggled on its own entity.  Instances are in the
 * entity's local space.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../graphics/lrg-foliage.h"
#include "../../graphics/lrg-terrain-asset.h"

G_BEGIN_DECLS

#define LRG_TYPE_FOLIAGE_COMPONENT (lrg_foliage_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFoliageComponent, lrg_foliage_component, LRG, FOLIAGE_COMPONENT, LrgComponent)

/**
 * lrg_foliage_component_new:
 *
 * Returns: (transfer full): a new #LrgFoliageComponent without foliage
 */
LRG_AVAILABLE_IN_ALL
LrgFoliageComponent * lrg_foliage_component_new (void);

/**
 * lrg_foliage_component_get_foliage_set:
 * @self: an #LrgFoliageComponent
 *
 * Returns: (transfer none) (nullable): the foliage drawn
 */
LRG_AVAILABLE_IN_ALL
LrgFoliage * lrg_foliage_component_get_foliage_set (LrgFoliageComponent *self);

/**
 * lrg_foliage_component_set_foliage_set:
 * @self: an #LrgFoliageComponent
 * @foliage: (nullable): the foliage to draw
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_component_set_foliage_set (LrgFoliageComponent *self,
                                            LrgFoliage          *foliage);

/**
 * lrg_foliage_component_get_terrain:
 * @self: an #LrgFoliageComponent
 *
 * Returns: (transfer none) (nullable): the ground the foliage scatters on
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainAsset * lrg_foliage_component_get_terrain (LrgFoliageComponent *self);

/**
 * lrg_foliage_component_set_terrain:
 * @self: an #LrgFoliageComponent
 * @terrain: (nullable): the terrain asset dirty patches re-scatter on;
 *   %NULL draws the instances as they are
 *
 * The foliage's grid follows the terrain's cells.
 */
LRG_AVAILABLE_IN_ALL
void lrg_foliage_component_set_terrain (LrgFoliageComponent *self,
                                        LrgTerrainAsset     *terrain);

G_END_DECLS
