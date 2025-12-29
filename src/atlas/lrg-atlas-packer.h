/* lrg-atlas-packer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Build-time texture atlas packer.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-texture-atlas.h"

G_BEGIN_DECLS

#define LRG_TYPE_ATLAS_PACKER (lrg_atlas_packer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAtlasPacker, lrg_atlas_packer, LRG, ATLAS_PACKER, GObject)

/**
 * LrgAtlasPackerImage:
 *
 * Information about an image to be packed.
 *
 * Since: 1.0
 */
typedef struct _LrgAtlasPackerImage LrgAtlasPackerImage;

/**
 * lrg_atlas_packer_new:
 *
 * Creates a new atlas packer.
 *
 * Returns: (transfer full): A new #LrgAtlasPacker
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasPacker *    lrg_atlas_packer_new                    (void);

/* Configuration */

/**
 * lrg_atlas_packer_set_max_size:
 * @self: A #LrgAtlasPacker
 * @width: Maximum atlas width
 * @height: Maximum atlas height
 *
 * Sets the maximum atlas dimensions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_atlas_packer_set_max_size           (LrgAtlasPacker *self,
                                                             gint            width,
                                                             gint            height);

/**
 * lrg_atlas_packer_get_max_width:
 * @self: A #LrgAtlasPacker
 *
 * Gets the maximum atlas width.
 *
 * Returns: Maximum width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_atlas_packer_get_max_width          (LrgAtlasPacker *self);

/**
 * lrg_atlas_packer_get_max_height:
 * @self: A #LrgAtlasPacker
 *
 * Gets the maximum atlas height.
 *
 * Returns: Maximum height in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_atlas_packer_get_max_height         (LrgAtlasPacker *self);

/**
 * lrg_atlas_packer_set_padding:
 * @self: A #LrgAtlasPacker
 * @padding: Padding between images in pixels
 *
 * Sets the padding between packed images.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_atlas_packer_set_padding            (LrgAtlasPacker *self,
                                                             gint            padding);

/**
 * lrg_atlas_packer_get_padding:
 * @self: A #LrgAtlasPacker
 *
 * Gets the padding between packed images.
 *
 * Returns: Padding in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_atlas_packer_get_padding            (LrgAtlasPacker *self);

/**
 * lrg_atlas_packer_set_method:
 * @self: A #LrgAtlasPacker
 * @method: The packing algorithm to use
 *
 * Sets the packing algorithm.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_atlas_packer_set_method             (LrgAtlasPacker   *self,
                                                             LrgAtlasPackMethod method);

/**
 * lrg_atlas_packer_get_method:
 * @self: A #LrgAtlasPacker
 *
 * Gets the packing algorithm.
 *
 * Returns: The packing method
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasPackMethod  lrg_atlas_packer_get_method             (LrgAtlasPacker *self);

/**
 * lrg_atlas_packer_set_power_of_two:
 * @self: A #LrgAtlasPacker
 * @power_of_two: Whether to use power-of-two dimensions
 *
 * Sets whether the output atlas should have power-of-two dimensions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_atlas_packer_set_power_of_two       (LrgAtlasPacker *self,
                                                             gboolean        power_of_two);

/**
 * lrg_atlas_packer_get_power_of_two:
 * @self: A #LrgAtlasPacker
 *
 * Gets whether power-of-two dimensions are required.
 *
 * Returns: %TRUE if power-of-two is enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_atlas_packer_get_power_of_two       (LrgAtlasPacker *self);

/**
 * lrg_atlas_packer_set_allow_rotation:
 * @self: A #LrgAtlasPacker
 * @allow: Whether to allow 90-degree rotation
 *
 * Sets whether images can be rotated 90 degrees to fit better.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_atlas_packer_set_allow_rotation     (LrgAtlasPacker *self,
                                                             gboolean        allow);

/**
 * lrg_atlas_packer_get_allow_rotation:
 * @self: A #LrgAtlasPacker
 *
 * Gets whether rotation is allowed.
 *
 * Returns: %TRUE if rotation is allowed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_atlas_packer_get_allow_rotation     (LrgAtlasPacker *self);

/* Image management */

/**
 * lrg_atlas_packer_add_image:
 * @self: A #LrgAtlasPacker
 * @name: Unique name for the image (used as region name)
 * @width: Image width in pixels
 * @height: Image height in pixels
 * @user_data: (nullable): User data to associate with this image
 *
 * Adds an image to be packed. The actual image data is not stored;
 * only dimensions are needed for packing.
 *
 * Returns: %TRUE if the image was added
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_atlas_packer_add_image              (LrgAtlasPacker *self,
                                                             const gchar    *name,
                                                             gint            width,
                                                             gint            height,
                                                             gpointer        user_data);

/**
 * lrg_atlas_packer_remove_image:
 * @self: A #LrgAtlasPacker
 * @name: Name of the image to remove
 *
 * Removes an image from the packer.
 *
 * Returns: %TRUE if the image was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_atlas_packer_remove_image           (LrgAtlasPacker *self,
                                                             const gchar    *name);

/**
 * lrg_atlas_packer_clear_images:
 * @self: A #LrgAtlasPacker
 *
 * Removes all images from the packer.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_atlas_packer_clear_images           (LrgAtlasPacker *self);

/**
 * lrg_atlas_packer_get_image_count:
 * @self: A #LrgAtlasPacker
 *
 * Gets the number of images to pack.
 *
 * Returns: Image count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_atlas_packer_get_image_count        (LrgAtlasPacker *self);

/* Packing */

/**
 * lrg_atlas_packer_pack:
 * @self: A #LrgAtlasPacker
 * @error: (nullable): Return location for error
 *
 * Performs the packing algorithm to arrange all images.
 * After packing, use lrg_atlas_packer_create_atlas() to get the result.
 *
 * Returns: %TRUE if packing succeeded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_atlas_packer_pack                   (LrgAtlasPacker  *self,
                                                             GError         **error);

/**
 * lrg_atlas_packer_get_packed_width:
 * @self: A #LrgAtlasPacker
 *
 * Gets the width of the packed atlas (available after pack()).
 *
 * Returns: Packed width in pixels, or 0 if not yet packed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_atlas_packer_get_packed_width       (LrgAtlasPacker *self);

/**
 * lrg_atlas_packer_get_packed_height:
 * @self: A #LrgAtlasPacker
 *
 * Gets the height of the packed atlas (available after pack()).
 *
 * Returns: Packed height in pixels, or 0 if not yet packed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_atlas_packer_get_packed_height      (LrgAtlasPacker *self);

/**
 * lrg_atlas_packer_get_efficiency:
 * @self: A #LrgAtlasPacker
 *
 * Gets the packing efficiency (used area / total area).
 *
 * Returns: Efficiency as a value 0.0-1.0, or 0 if not yet packed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_atlas_packer_get_efficiency         (LrgAtlasPacker *self);

/* Result access */

/**
 * lrg_atlas_packer_create_atlas:
 * @self: A #LrgAtlasPacker
 * @name: Name for the atlas
 *
 * Creates a texture atlas from the packed result.
 * Must call lrg_atlas_packer_pack() first.
 *
 * Returns: (transfer full) (nullable): A new #LrgTextureAtlas, or %NULL if not packed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTextureAtlas *   lrg_atlas_packer_create_atlas           (LrgAtlasPacker *self,
                                                             const gchar    *name);

/**
 * lrg_atlas_packer_get_image_position:
 * @self: A #LrgAtlasPacker
 * @name: Name of the image
 * @out_x: (out) (nullable): Return location for X position
 * @out_y: (out) (nullable): Return location for Y position
 * @out_rotated: (out) (nullable): Return location for rotation flag
 *
 * Gets the packed position of an image.
 * Must call lrg_atlas_packer_pack() first.
 *
 * Returns: %TRUE if the image was found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_atlas_packer_get_image_position     (LrgAtlasPacker *self,
                                                             const gchar    *name,
                                                             gint           *out_x,
                                                             gint           *out_y,
                                                             gboolean       *out_rotated);

/**
 * lrg_atlas_packer_get_image_user_data:
 * @self: A #LrgAtlasPacker
 * @name: Name of the image
 *
 * Gets the user data associated with an image.
 *
 * Returns: (transfer none) (nullable): The user data, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gpointer            lrg_atlas_packer_get_image_user_data    (LrgAtlasPacker *self,
                                                             const gchar    *name);

/**
 * lrg_atlas_packer_foreach_image:
 * @self: A #LrgAtlasPacker
 * @func: (scope call): Function to call for each image
 * @user_data: User data to pass to the function
 *
 * Iterates over all packed images with their positions.
 * Must call lrg_atlas_packer_pack() first.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_atlas_packer_foreach_image          (LrgAtlasPacker *self,
                                                             GFunc           func,
                                                             gpointer        user_data);

G_END_DECLS
