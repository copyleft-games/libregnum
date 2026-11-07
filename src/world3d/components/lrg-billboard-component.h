/* lrg-billboard-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBillboardComponent: a textured quad (sprite) at the entity, either
 * facing the camera (%LRG_RENDER_ITEM_BILLBOARD items) or fixed in the
 * entity's local XY plane like the editor's SPRITE visual.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../ecs/lrg-component.h"
#include "../lrg-world3d.h"

G_BEGIN_DECLS

/**
 * LrgBillboardFacing:
 * @LRG_BILLBOARD_FACING_SPHERICAL: the quad faces the camera fully
 * @LRG_BILLBOARD_FACING_AXIS_Y: the quad turns about the world Y axis
 *   only (trees, characters)
 * @LRG_BILLBOARD_FACING_FIXED: the quad lies in the entity's local XY
 *   plane facing +Z and turns with the entity (the editor's SPRITE visual)
 *
 * How an #LrgBillboardComponent's quad is oriented.  The spherical and
 * axis-Y modes travel to the renderer as the matching #LrgBillboardMode
 * value (%LRG_BILLBOARD_MODE_SPHERICAL or %LRG_BILLBOARD_MODE_AXIS_Y) in
 * #LrgRenderItem.params[2] of a %LRG_RENDER_ITEM_BILLBOARD item; the fixed
 * mode is a textured %LRG_PRIMITIVE_PLANE item.
 */
typedef enum
{
	LRG_BILLBOARD_FACING_SPHERICAL,
	LRG_BILLBOARD_FACING_AXIS_Y,
	LRG_BILLBOARD_FACING_FIXED
} LrgBillboardFacing;

LRG_AVAILABLE_IN_ALL
GType lrg_billboard_facing_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BILLBOARD_FACING (lrg_billboard_facing_get_type ())

#define LRG_TYPE_BILLBOARD_COMPONENT (lrg_billboard_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBillboardComponent, lrg_billboard_component,
                      LRG, BILLBOARD_COMPONENT, LrgComponent)

/**
 * lrg_billboard_component_new:
 *
 * Creates a 1 x 1 white spherical billboard, alpha tested, no shadows.
 *
 * Returns: (transfer full): a new #LrgBillboardComponent
 */
LRG_AVAILABLE_IN_ALL
LrgBillboardComponent * lrg_billboard_component_new (void);

/**
 * lrg_billboard_component_new_with_texture_path:
 * @path: an image path or asset GUID
 *
 * Creates a billboard showing the image at @path.
 *
 * Returns: (transfer full): a new #LrgBillboardComponent
 */
LRG_AVAILABLE_IN_ALL
LrgBillboardComponent * lrg_billboard_component_new_with_texture_path (const gchar *path);

/**
 * lrg_billboard_component_get_texture:
 * @self: an #LrgBillboardComponent
 *
 * Gets the texture the items carry: explicit, or resolved from the
 * texture path through #LrgBillboardComponent:asset-manager.
 *
 * Returns: (transfer none) (nullable): the texture
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_billboard_component_get_texture (LrgBillboardComponent *self);

/**
 * lrg_billboard_component_set_texture:
 * @self: an #LrgBillboardComponent
 * @texture: (nullable): a loaded texture (referenced)
 *
 * Shows an already loaded texture; %NULL resolves the texture path again.
 */
LRG_AVAILABLE_IN_ALL
void lrg_billboard_component_set_texture (LrgBillboardComponent *self,
                                          GrlTexture            *texture);

/**
 * lrg_billboard_component_get_texture_path:
 * @self: an #LrgBillboardComponent
 *
 * Returns: (transfer none) (nullable): the image path or asset GUID
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_billboard_component_get_texture_path (LrgBillboardComponent *self);

/**
 * lrg_billboard_component_set_texture_path:
 * @self: an #LrgBillboardComponent
 * @path: (nullable): an image path or asset GUID
 *
 * Sets the image.  It is loaded through the asset manager when one is set
 * (needs a graphics context); otherwise items go out without a texture and
 * the renderer may resolve the path itself.
 */
LRG_AVAILABLE_IN_ALL
void lrg_billboard_component_set_texture_path (LrgBillboardComponent *self,
                                               const gchar           *path);

/**
 * lrg_billboard_component_set_asset_manager:
 * @self: an #LrgBillboardComponent
 * @manager: (nullable): an asset manager
 *
 * Sets the asset manager the texture path resolves through.
 */
LRG_AVAILABLE_IN_ALL
void lrg_billboard_component_set_asset_manager (LrgBillboardComponent *self,
                                                LrgAssetManager       *manager);

/**
 * lrg_billboard_component_set_size:
 * @self: an #LrgBillboardComponent
 * @width: quad width in world units
 * @height: quad height in world units
 *
 * Sets the quad size (used when #LrgBillboardComponent:pixels-per-unit is
 * 0 or no texture is known).
 */
LRG_AVAILABLE_IN_ALL
void lrg_billboard_component_set_size (LrgBillboardComponent *self,
                                       gfloat                 width,
                                       gfloat                 height);

/**
 * lrg_billboard_component_get_effective_size:
 * @self: an #LrgBillboardComponent
 * @out_width: (out) (optional): the drawn width
 * @out_height: (out) (optional): the drawn height
 *
 * Gets the drawn size: the texture size divided by
 * #LrgBillboardComponent:pixels-per-unit when both are known, otherwise
 * #LrgBillboardComponent:width x #LrgBillboardComponent:height (shrunk on
 * one axis to the texture's aspect ratio with
 * #LrgBillboardComponent:preserve-aspect).
 */
LRG_AVAILABLE_IN_ALL
void lrg_billboard_component_get_effective_size (LrgBillboardComponent *self,
                                                 gfloat                *out_width,
                                                 gfloat                *out_height);

/**
 * lrg_billboard_component_get_facing:
 * @self: an #LrgBillboardComponent
 *
 * Returns: how the quad is oriented
 */
LRG_AVAILABLE_IN_ALL
LrgBillboardFacing lrg_billboard_component_get_facing (LrgBillboardComponent *self);

/**
 * lrg_billboard_component_set_facing:
 * @self: an #LrgBillboardComponent
 * @facing: the orientation mode
 *
 * Sets the orientation mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_billboard_component_set_facing (LrgBillboardComponent *self,
                                         LrgBillboardFacing     facing);

/**
 * lrg_billboard_component_set_color:
 * @self: an #LrgBillboardComponent
 * @r: red, 0 .. 1
 * @g: green, 0 .. 1
 * @b: blue, 0 .. 1
 * @a: alpha, 0 .. 1
 *
 * Sets the tint.
 */
LRG_AVAILABLE_IN_ALL
void lrg_billboard_component_set_color (LrgBillboardComponent *self,
                                        gfloat                 r,
                                        gfloat                 g,
                                        gfloat                 b,
                                        gfloat                 a);

G_END_DECLS
