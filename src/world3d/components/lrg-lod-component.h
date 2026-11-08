/* lrg-lod-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgLodComponent: draws an #LrgLodGroup at the owning entity. In
 * collect_render it measures the entity's screen size from the render
 * list's camera, selects the level (hysteresis, bias, forced level) and
 * submits one MODEL item per model of the level, plus the level fading
 * out during a cross-fade. The `lodFade` value of each item is read with
 * lrg_lod_component_get_item_fade(). The group is either set directly or
 * generated from a model asset and its `.meta` LOD keys.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../../graphics/lrg-lod-group.h"
#include "../../core/lrg-asset-manager.h"
#include "../lrg-world3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_LOD_COMPONENT (lrg_lod_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLodComponent, lrg_lod_component, LRG, LOD_COMPONENT, LrgComponent)

/**
 * lrg_lod_component_new:
 *
 * Creates a component without a group (it draws nothing until a group
 * or a mesh asset with an asset manager is set). Shadows on, white tint.
 *
 * Returns: (transfer full): a new #LrgLodComponent
 */
LRG_AVAILABLE_IN_ALL
LrgLodComponent * lrg_lod_component_new (void);

/**
 * lrg_lod_component_new_with_group:
 * @group: the LOD chain to draw
 *
 * Returns: (transfer full): a new #LrgLodComponent drawing @group
 */
LRG_AVAILABLE_IN_ALL
LrgLodComponent * lrg_lod_component_new_with_group (LrgLodGroup *group);

/* --------------------------------------------------------------------------
 * Source
 * -------------------------------------------------------------------------- */

/**
 * lrg_lod_component_get_group:
 * @self: an #LrgLodComponent
 *
 * Returns: (transfer none) (nullable): the group (set, or generated from
 *   #LrgLodComponent:mesh-asset)
 */
LRG_AVAILABLE_IN_ALL
LrgLodGroup * lrg_lod_component_get_group (LrgLodComponent *self);

/**
 * lrg_lod_component_set_group:
 * @self: an #LrgLodComponent
 * @group: (nullable): the LOD chain to draw
 *
 * Sets #LrgLodComponent:group explicitly (overrides the mesh asset).
 * Every view's selection state restarts.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_set_group (LrgLodComponent *self,
                                  LrgLodGroup     *group);

/**
 * lrg_lod_component_get_mesh_asset:
 * @self: an #LrgLodComponent
 *
 * Returns: (transfer none) (nullable): the model asset path or `guid:`
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_lod_component_get_mesh_asset (LrgLodComponent *self);

/**
 * lrg_lod_component_set_mesh_asset:
 * @self: an #LrgLodComponent
 * @asset: (nullable): a model asset for #LrgLodComponent:asset-manager
 *
 * Sets #LrgLodComponent:mesh-asset. On the first draw the model is loaded
 * through the asset manager and its LOD chain generated from the `.meta`
 * keys (`lod-levels`, `lod-ratios`, `lod-screen-sizes`, ...). Components
 * with the same model and settings share one generated group.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_set_mesh_asset (LrgLodComponent *self,
                                       const gchar     *asset);

/**
 * lrg_lod_component_get_asset_manager:
 * @self: an #LrgLodComponent
 *
 * Returns: (transfer none) (nullable): the asset manager
 */
LRG_AVAILABLE_IN_ALL
LrgAssetManager * lrg_lod_component_get_asset_manager (LrgLodComponent *self);

/**
 * lrg_lod_component_set_asset_manager:
 * @self: an #LrgLodComponent
 * @manager: (nullable): resolves #LrgLodComponent:mesh-asset
 *
 * Sets #LrgLodComponent:asset-manager.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_set_asset_manager (LrgLodComponent *self,
                                          LrgAssetManager *manager);

/* --------------------------------------------------------------------------
 * Selection overrides
 * -------------------------------------------------------------------------- */

/**
 * lrg_lod_component_get_force_level:
 * @self: an #LrgLodComponent
 *
 * Returns: the level this component always draws, or -1
 */
LRG_AVAILABLE_IN_ALL
gint lrg_lod_component_get_force_level (LrgLodComponent *self);

/**
 * lrg_lod_component_set_force_level:
 * @self: an #LrgLodComponent
 * @level: level to always draw (clamped to the group), or -1
 *
 * Sets #LrgLodComponent:force-level: a per-instance override of the
 * group's own forced level (the group may be shared).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_set_force_level (LrgLodComponent *self,
                                        gint             level);

/**
 * lrg_lod_component_get_bias:
 * @self: an #LrgLodComponent
 *
 * Returns: the per-instance screen-size multiplier
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_component_get_bias (LrgLodComponent *self);

/**
 * lrg_lod_component_set_bias:
 * @self: an #LrgLodComponent
 * @bias: screen-size multiplier (> 0), on top of the group and global
 *   biases
 *
 * Sets #LrgLodComponent:bias (default 1).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_set_bias (LrgLodComponent *self,
                                 gfloat           bias);

/* --------------------------------------------------------------------------
 * Render state
 * -------------------------------------------------------------------------- */

/**
 * lrg_lod_component_get_cast_shadows:
 * @self: an #LrgLodComponent
 *
 * Returns: whether the current level casts shadows
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lod_component_get_cast_shadows (LrgLodComponent *self);

/**
 * lrg_lod_component_set_cast_shadows:
 * @self: an #LrgLodComponent
 * @cast: whether the current level casts shadows (the level fading out
 *   never does, so a cross-fade does not double the shadow)
 *
 * Sets #LrgLodComponent:cast-shadows (default %TRUE).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_set_cast_shadows (LrgLodComponent *self,
                                         gboolean         cast);

/**
 * lrg_lod_component_get_receive_shadows:
 * @self: an #LrgLodComponent
 *
 * Returns: whether the items are lit with shadow lookups
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_lod_component_get_receive_shadows (LrgLodComponent *self);

/**
 * lrg_lod_component_set_receive_shadows:
 * @self: an #LrgLodComponent
 * @receive: whether the items receive shadows
 *
 * Sets #LrgLodComponent:receive-shadows (default %TRUE).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_set_receive_shadows (LrgLodComponent *self,
                                            gboolean         receive);

/**
 * lrg_lod_component_get_layer:
 * @self: an #LrgLodComponent
 *
 * Returns: the render layer override, or -1 for the entity's layer
 */
LRG_AVAILABLE_IN_ALL
gint lrg_lod_component_get_layer (LrgLodComponent *self);

/**
 * lrg_lod_component_set_layer:
 * @self: an #LrgLodComponent
 * @layer: render layer 0 .. 31, or -1 for the entity's layer
 *
 * Sets #LrgLodComponent:layer.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_set_layer (LrgLodComponent *self,
                                  gint             layer);

/**
 * lrg_lod_component_get_color:
 * @self: an #LrgLodComponent
 * @out_rgba: (out caller-allocates) (array fixed-size=4): RGBA tint, 0..1
 *
 * Reads the tint multiplied into every level's material colour.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_get_color (LrgLodComponent *self,
                                  gfloat          *out_rgba);

/**
 * lrg_lod_component_set_color:
 * @self: an #LrgLodComponent
 * @r: red 0..1
 * @g: green 0..1
 * @b: blue 0..1
 * @a: alpha 0..1
 *
 * Sets the tint (properties `color-r` .. `color-a`).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_set_color (LrgLodComponent *self,
                                  gfloat           r,
                                  gfloat           g,
                                  gfloat           b,
                                  gfloat           a);

/**
 * lrg_lod_component_get_material:
 * @self: an #LrgLodComponent
 *
 * Returns: (transfer none) (nullable): the material override passed on
 *   every item (an LrgMaterial or LrgMaterial3D), or %NULL to use the
 *   models' own materials
 */
LRG_AVAILABLE_IN_ALL
GObject * lrg_lod_component_get_material (LrgLodComponent *self);

/**
 * lrg_lod_component_set_material:
 * @self: an #LrgLodComponent
 * @material: (nullable): a material override
 *
 * Sets #LrgLodComponent:material.
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_set_material (LrgLodComponent *self,
                                     GObject         *material);

/* --------------------------------------------------------------------------
 * Selection results
 * -------------------------------------------------------------------------- */

/**
 * lrg_lod_component_get_current_level:
 * @self: an #LrgLodComponent
 *
 * Returns: the level chosen by the last collect_render (any view), or
 *   %LRG_LOD_LEVEL_CULLED (also before the first draw)
 */
LRG_AVAILABLE_IN_ALL
gint lrg_lod_component_get_current_level (LrgLodComponent *self);

/**
 * lrg_lod_component_get_screen_size:
 * @self: an #LrgLodComponent
 *
 * Returns: the biased screen size measured by the last collect_render
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_component_get_screen_size (LrgLodComponent *self);

/**
 * lrg_lod_component_get_state_for_list:
 * @self: an #LrgLodComponent
 * @list: the render list (one per view) the state belongs to
 *
 * Returns: (transfer none) (nullable): the selection and fade state of
 *   @list's view, or %NULL if @list never collected this component
 */
LRG_AVAILABLE_IN_ALL
const LrgLodState * lrg_lod_component_get_state_for_list (LrgLodComponent *self,
                                                          LrgRenderList   *list);

/**
 * lrg_lod_component_reset_states:
 * @self: an #LrgLodComponent
 *
 * Forgets every view's selection (the next draw selects without
 * hysteresis or fade, as after a camera cut or a teleport).
 */
LRG_AVAILABLE_IN_ALL
void lrg_lod_component_reset_states (LrgLodComponent *self);

/**
 * lrg_lod_component_get_item_fade:
 * @item: a render item from any component
 * @out_level: (out) (optional): the LOD level the item draws, or -1
 *
 * For renderers: the `lodFade` value to bind (lrg_lod_bind_fade()) before
 * drawing @item. Items of an #LrgLodComponent carry it in their
 * `user_data` (packed float bits, no destroy notify); every other item
 * draws with 0.
 *
 * Returns: the fade (0 = opaque, > 0 fading in, < 0 fading out)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_lod_component_get_item_fade (const LrgRenderItem *item,
                                        gint                *out_level);

G_END_DECLS
