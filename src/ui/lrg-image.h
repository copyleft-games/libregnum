/* lrg-image.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Image widget for displaying textures.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_IMAGE (lrg_image_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgImage, lrg_image, LRG, IMAGE, LrgWidget)

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_image_new:
 *
 * Creates a new image widget without a texture.
 *
 * Returns: (transfer full): A new #LrgImage
 */
LRG_AVAILABLE_IN_ALL
LrgImage * lrg_image_new (void);

/**
 * lrg_image_new_with_texture:
 * @texture: (nullable): the texture to display
 *
 * Creates a new image widget with the specified texture.
 *
 * Returns: (transfer full): A new #LrgImage
 */
LRG_AVAILABLE_IN_ALL
LrgImage * lrg_image_new_with_texture (GrlTexture *texture);

/* ==========================================================================
 * Texture
 * ========================================================================== */

/**
 * lrg_image_get_texture:
 * @self: an #LrgImage
 *
 * Gets the texture being displayed.
 *
 * Returns: (transfer none) (nullable): The texture, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_image_get_texture (LrgImage *self);

/**
 * lrg_image_set_texture:
 * @self: an #LrgImage
 * @texture: (nullable): the texture to display
 *
 * Sets the texture to display.
 */
LRG_AVAILABLE_IN_ALL
void lrg_image_set_texture (LrgImage   *self,
                            GrlTexture *texture);

/* ==========================================================================
 * Scale Mode
 * ========================================================================== */

/**
 * lrg_image_get_scale_mode:
 * @self: an #LrgImage
 *
 * Gets the image scaling mode.
 *
 * Returns: The scale mode
 */
LRG_AVAILABLE_IN_ALL
LrgImageScaleMode lrg_image_get_scale_mode (LrgImage *self);

/**
 * lrg_image_set_scale_mode:
 * @self: an #LrgImage
 * @mode: the scale mode
 *
 * Sets how the image is scaled to fit the widget bounds.
 *
 * - %LRG_IMAGE_SCALE_MODE_FIT: Scale to fit, maintaining aspect ratio
 * - %LRG_IMAGE_SCALE_MODE_FILL: Scale to fill, cropping if needed
 * - %LRG_IMAGE_SCALE_MODE_STRETCH: Stretch to exact widget size
 * - %LRG_IMAGE_SCALE_MODE_TILE: Tile the texture to fill the area
 */
LRG_AVAILABLE_IN_ALL
void lrg_image_set_scale_mode (LrgImage          *self,
                               LrgImageScaleMode  mode);

/* ==========================================================================
 * Tint
 * ========================================================================== */

/**
 * lrg_image_get_tint:
 * @self: an #LrgImage
 *
 * Gets the color tint applied to the texture.
 *
 * Returns: (transfer none): The tint color
 */
LRG_AVAILABLE_IN_ALL
const GrlColor * lrg_image_get_tint (LrgImage *self);

/**
 * lrg_image_set_tint:
 * @self: an #LrgImage
 * @tint: the tint color
 *
 * Sets the color tint applied to the texture.
 * Use white (255, 255, 255, 255) for no tint.
 */
LRG_AVAILABLE_IN_ALL
void lrg_image_set_tint (LrgImage       *self,
                         const GrlColor *tint);

/* ==========================================================================
 * Source Rectangle
 * ========================================================================== */

/**
 * lrg_image_get_source_rect:
 * @self: an #LrgImage
 *
 * Gets the source rectangle for sprite sheet rendering.
 *
 * Returns: (transfer none) (nullable): The source rectangle, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const GrlRectangle * lrg_image_get_source_rect (LrgImage *self);

/**
 * lrg_image_set_source_rect:
 * @self: an #LrgImage
 * @rect: (nullable): the source rectangle, or %NULL for whole texture
 *
 * Sets the source rectangle to draw from the texture.
 * This is useful for sprite sheets and texture atlases.
 * Pass %NULL to draw the entire texture.
 */
LRG_AVAILABLE_IN_ALL
void lrg_image_set_source_rect (LrgImage           *self,
                                const GrlRectangle *rect);

/**
 * lrg_image_clear_source_rect:
 * @self: an #LrgImage
 *
 * Clears the source rectangle so the entire texture is drawn.
 */
LRG_AVAILABLE_IN_ALL
void lrg_image_clear_source_rect (LrgImage *self);

G_END_DECLS
