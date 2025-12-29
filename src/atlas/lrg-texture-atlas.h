/* lrg-texture-atlas.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Texture atlas for efficient sprite rendering.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-atlas-region.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEXTURE_ATLAS (lrg_texture_atlas_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTextureAtlas, lrg_texture_atlas, LRG, TEXTURE_ATLAS, GObject)

/**
 * lrg_texture_atlas_new:
 * @name: Name identifier for the atlas
 *
 * Creates a new empty texture atlas.
 *
 * Returns: (transfer full): A new #LrgTextureAtlas
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTextureAtlas *   lrg_texture_atlas_new               (const gchar *name);

/**
 * lrg_texture_atlas_new_from_file:
 * @path: Path to the atlas definition file (YAML)
 * @error: (nullable): Return location for error
 *
 * Creates a texture atlas by loading a definition file.
 *
 * Returns: (transfer full) (nullable): A new #LrgTextureAtlas or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTextureAtlas *   lrg_texture_atlas_new_from_file     (const gchar  *path,
                                                         GError      **error);

/* Properties */

/**
 * lrg_texture_atlas_get_name:
 * @self: A #LrgTextureAtlas
 *
 * Gets the name of the atlas.
 *
 * Returns: (transfer none) (nullable): The atlas name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_texture_atlas_get_name          (LrgTextureAtlas *self);

/**
 * lrg_texture_atlas_get_texture_path:
 * @self: A #LrgTextureAtlas
 *
 * Gets the path to the texture file.
 *
 * Returns: (transfer none) (nullable): The texture path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_texture_atlas_get_texture_path  (LrgTextureAtlas *self);

/**
 * lrg_texture_atlas_set_texture_path:
 * @self: A #LrgTextureAtlas
 * @path: Path to the texture file
 *
 * Sets the path to the texture file.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_texture_atlas_set_texture_path  (LrgTextureAtlas *self,
                                                         const gchar     *path);

/**
 * lrg_texture_atlas_get_width:
 * @self: A #LrgTextureAtlas
 *
 * Gets the width of the atlas texture.
 *
 * Returns: Width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_texture_atlas_get_width         (LrgTextureAtlas *self);

/**
 * lrg_texture_atlas_set_width:
 * @self: A #LrgTextureAtlas
 * @width: Width in pixels
 *
 * Sets the width of the atlas texture.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_texture_atlas_set_width         (LrgTextureAtlas *self,
                                                         gint             width);

/**
 * lrg_texture_atlas_get_height:
 * @self: A #LrgTextureAtlas
 *
 * Gets the height of the atlas texture.
 *
 * Returns: Height in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_texture_atlas_get_height        (LrgTextureAtlas *self);

/**
 * lrg_texture_atlas_set_height:
 * @self: A #LrgTextureAtlas
 * @height: Height in pixels
 *
 * Sets the height of the atlas texture.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_texture_atlas_set_height        (LrgTextureAtlas *self,
                                                         gint             height);

/**
 * lrg_texture_atlas_set_size:
 * @self: A #LrgTextureAtlas
 * @width: Width in pixels
 * @height: Height in pixels
 *
 * Sets both dimensions of the atlas texture.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_texture_atlas_set_size          (LrgTextureAtlas *self,
                                                         gint             width,
                                                         gint             height);

/* Region management */

/**
 * lrg_texture_atlas_add_region:
 * @self: A #LrgTextureAtlas
 * @region: The region to add
 *
 * Adds a region to the atlas. The atlas takes ownership of the region.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_texture_atlas_add_region        (LrgTextureAtlas      *self,
                                                         LrgAtlasRegion       *region);

/**
 * lrg_texture_atlas_add_region_rect:
 * @self: A #LrgTextureAtlas
 * @name: Name of the region
 * @x: X position
 * @y: Y position
 * @width: Width
 * @height: Height
 *
 * Convenience function to add a region by rectangle.
 * UV coordinates are calculated automatically.
 *
 * Returns: (transfer none): The added region
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasRegion *    lrg_texture_atlas_add_region_rect   (LrgTextureAtlas *self,
                                                         const gchar     *name,
                                                         gint             x,
                                                         gint             y,
                                                         gint             width,
                                                         gint             height);

/**
 * lrg_texture_atlas_remove_region:
 * @self: A #LrgTextureAtlas
 * @name: Name of the region to remove
 *
 * Removes a region from the atlas.
 *
 * Returns: %TRUE if the region was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_texture_atlas_remove_region     (LrgTextureAtlas *self,
                                                         const gchar     *name);

/**
 * lrg_texture_atlas_get_region:
 * @self: A #LrgTextureAtlas
 * @name: Name of the region
 *
 * Gets a region by name.
 *
 * Returns: (transfer none) (nullable): The region, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasRegion *    lrg_texture_atlas_get_region        (LrgTextureAtlas *self,
                                                         const gchar     *name);

/**
 * lrg_texture_atlas_has_region:
 * @self: A #LrgTextureAtlas
 * @name: Name of the region
 *
 * Checks if a region exists.
 *
 * Returns: %TRUE if the region exists
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_texture_atlas_has_region        (LrgTextureAtlas *self,
                                                         const gchar     *name);

/**
 * lrg_texture_atlas_get_region_count:
 * @self: A #LrgTextureAtlas
 *
 * Gets the number of regions.
 *
 * Returns: The region count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_texture_atlas_get_region_count  (LrgTextureAtlas *self);

/**
 * lrg_texture_atlas_get_region_names:
 * @self: A #LrgTextureAtlas
 *
 * Gets all region names.
 *
 * Returns: (transfer full) (element-type utf8): Array of region names
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_texture_atlas_get_region_names  (LrgTextureAtlas *self);

/**
 * lrg_texture_atlas_clear_regions:
 * @self: A #LrgTextureAtlas
 *
 * Removes all regions from the atlas.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_texture_atlas_clear_regions     (LrgTextureAtlas *self);

/* UV calculation */

/**
 * lrg_texture_atlas_recalculate_uvs:
 * @self: A #LrgTextureAtlas
 *
 * Recalculates UV coordinates for all regions based on their
 * pixel positions and the atlas dimensions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_texture_atlas_recalculate_uvs   (LrgTextureAtlas *self);

/* Serialization */

/**
 * lrg_texture_atlas_save_to_file:
 * @self: A #LrgTextureAtlas
 * @path: Path to save to
 * @error: (nullable): Return location for error
 *
 * Saves the atlas definition to a YAML file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_texture_atlas_save_to_file      (LrgTextureAtlas  *self,
                                                         const gchar      *path,
                                                         GError          **error);

G_END_DECLS
