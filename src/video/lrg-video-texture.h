/* lrg-video-texture.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_VIDEO_TEXTURE (lrg_video_texture_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgVideoTexture, lrg_video_texture, LRG, VIDEO_TEXTURE, GObject)

/**
 * lrg_video_texture_new:
 * @width: texture width in pixels
 * @height: texture height in pixels
 *
 * Creates a new video texture with the given dimensions.
 *
 * Returns: (transfer full): A new #LrgVideoTexture
 */
LRG_AVAILABLE_IN_ALL
LrgVideoTexture *lrg_video_texture_new (guint width,
                                        guint height);

/**
 * lrg_video_texture_get_width:
 * @texture: an #LrgVideoTexture
 *
 * Gets the texture width.
 *
 * Returns: The texture width in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_video_texture_get_width (LrgVideoTexture *texture);

/**
 * lrg_video_texture_get_height:
 * @texture: an #LrgVideoTexture
 *
 * Gets the texture height.
 *
 * Returns: The texture height in pixels
 */
LRG_AVAILABLE_IN_ALL
guint lrg_video_texture_get_height (LrgVideoTexture *texture);

/**
 * lrg_video_texture_update:
 * @texture: an #LrgVideoTexture
 * @data: (array length=size): RGBA pixel data
 * @size: size of pixel data in bytes
 *
 * Updates the texture with new frame data.
 * The data must be in RGBA format with 4 bytes per pixel.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_texture_update (LrgVideoTexture *texture,
                               const guint8    *data,
                               gsize            size);

/**
 * lrg_video_texture_clear:
 * @texture: an #LrgVideoTexture
 *
 * Clears the texture to black.
 */
LRG_AVAILABLE_IN_ALL
void lrg_video_texture_clear (LrgVideoTexture *texture);

/**
 * lrg_video_texture_is_valid:
 * @texture: an #LrgVideoTexture
 *
 * Checks if the texture has valid dimensions and data.
 *
 * Returns: %TRUE if the texture is valid
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_video_texture_is_valid (LrgVideoTexture *texture);

/**
 * lrg_video_texture_get_data:
 * @texture: an #LrgVideoTexture
 * @size: (out) (optional): location to store data size
 *
 * Gets a pointer to the raw texture data.
 *
 * Returns: (transfer none) (array length=size) (nullable): The texture data
 */
LRG_AVAILABLE_IN_ALL
const guint8 *lrg_video_texture_get_data (LrgVideoTexture *texture,
                                          gsize           *size);

G_END_DECLS
