/* lrg-sprite-component.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sprite rendering component.
 *
 * LrgSpriteComponent renders a texture at the game object's position.
 * It supports sprite sheets (via source rectangle), tinting, and flipping.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>

#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../lrg-component.h"

G_BEGIN_DECLS

#define LRG_TYPE_SPRITE_COMPONENT (lrg_sprite_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpriteComponent, lrg_sprite_component,
                      LRG, SPRITE_COMPONENT, LrgComponent)

/*
 * Construction
 */

/**
 * lrg_sprite_component_new:
 *
 * Creates a new sprite component with no texture.
 *
 * Returns: (transfer full): A new #LrgSpriteComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteComponent * lrg_sprite_component_new            (void);

/**
 * lrg_sprite_component_new_with_texture:
 * @texture: (nullable): initial texture
 *
 * Creates a new sprite component with the specified texture.
 *
 * Returns: (transfer full): A new #LrgSpriteComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSpriteComponent * lrg_sprite_component_new_with_texture (GrlTexture *texture);

/*
 * Texture Management
 */

/**
 * lrg_sprite_component_set_texture:
 * @self: an #LrgSpriteComponent
 * @texture: (nullable): the texture to use
 *
 * Sets the texture to render.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_sprite_component_set_texture  (LrgSpriteComponent *self,
                                                 GrlTexture         *texture);

/**
 * lrg_sprite_component_get_texture:
 * @self: an #LrgSpriteComponent
 *
 * Gets the current texture.
 *
 * Returns: (transfer none) (nullable): The texture, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GrlTexture *  lrg_sprite_component_get_texture  (LrgSpriteComponent *self);

/*
 * Source Rectangle (for sprite sheets)
 */

/**
 * lrg_sprite_component_set_source:
 * @self: an #LrgSpriteComponent
 * @x: source X coordinate in pixels
 * @y: source Y coordinate in pixels
 * @width: source width in pixels
 * @height: source height in pixels
 *
 * Sets the source rectangle for sprite sheet rendering.
 *
 * If not set, the entire texture is rendered.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_sprite_component_set_source   (LrgSpriteComponent *self,
                                                 gint                x,
                                                 gint                y,
                                                 gint                width,
                                                 gint                height);

/**
 * lrg_sprite_component_get_source:
 * @self: an #LrgSpriteComponent
 *
 * Gets the source rectangle.
 *
 * Returns: (transfer full) (nullable): The source rectangle, or %NULL if using full texture
 */
LRG_AVAILABLE_IN_ALL
GrlRectangle * lrg_sprite_component_get_source  (LrgSpriteComponent *self);

/**
 * lrg_sprite_component_clear_source:
 * @self: an #LrgSpriteComponent
 *
 * Clears the source rectangle, causing the full texture to be rendered.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_sprite_component_clear_source (LrgSpriteComponent *self);

/*
 * Tint Color
 */

/**
 * lrg_sprite_component_set_tint:
 * @self: an #LrgSpriteComponent
 * @color: the tint color
 *
 * Sets the tint color applied to the texture.
 *
 * White (255, 255, 255, 255) means no tinting.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_sprite_component_set_tint     (LrgSpriteComponent *self,
                                                 GrlColor           *color);

/**
 * lrg_sprite_component_get_tint:
 * @self: an #LrgSpriteComponent
 *
 * Gets the current tint color.
 *
 * Returns: (transfer full): The tint color
 */
LRG_AVAILABLE_IN_ALL
GrlColor *    lrg_sprite_component_get_tint     (LrgSpriteComponent *self);

/*
 * Flip
 */

/**
 * lrg_sprite_component_set_flip_h:
 * @self: an #LrgSpriteComponent
 * @flip: whether to flip horizontally
 *
 * Sets whether the sprite is flipped horizontally.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_sprite_component_set_flip_h   (LrgSpriteComponent *self,
                                                 gboolean            flip);

/**
 * lrg_sprite_component_get_flip_h:
 * @self: an #LrgSpriteComponent
 *
 * Gets whether the sprite is flipped horizontally.
 *
 * Returns: %TRUE if flipped horizontally
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_sprite_component_get_flip_h   (LrgSpriteComponent *self);

/**
 * lrg_sprite_component_set_flip_v:
 * @self: an #LrgSpriteComponent
 * @flip: whether to flip vertically
 *
 * Sets whether the sprite is flipped vertically.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_sprite_component_set_flip_v   (LrgSpriteComponent *self,
                                                 gboolean            flip);

/**
 * lrg_sprite_component_get_flip_v:
 * @self: an #LrgSpriteComponent
 *
 * Gets whether the sprite is flipped vertically.
 *
 * Returns: %TRUE if flipped vertically
 */
LRG_AVAILABLE_IN_ALL
gboolean      lrg_sprite_component_get_flip_v   (LrgSpriteComponent *self);

/*
 * Drawing
 */

/**
 * lrg_sprite_component_draw:
 * @self: an #LrgSpriteComponent
 *
 * Draws the sprite at the owning game object's position.
 *
 * This is typically called by the game object during its draw phase.
 * The component must be attached to a game object for this to work.
 */
LRG_AVAILABLE_IN_ALL
void          lrg_sprite_component_draw         (LrgSpriteComponent *self);

G_END_DECLS
