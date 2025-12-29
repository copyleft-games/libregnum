/* lrg-atlas-region.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Texture atlas region structure.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ATLAS_REGION (lrg_atlas_region_get_type ())

/**
 * LrgAtlasRegion:
 *
 * A region within a texture atlas.
 *
 * #LrgAtlasRegion defines a rectangular area within an atlas texture,
 * including UV coordinates for rendering and optional rotation/flip info.
 *
 * Since: 1.0
 */
typedef struct _LrgAtlasRegion LrgAtlasRegion;

LRG_AVAILABLE_IN_ALL
GType           lrg_atlas_region_get_type       (void) G_GNUC_CONST;

/**
 * lrg_atlas_region_new:
 * @name: Region name/identifier
 * @x: X position in the atlas (pixels)
 * @y: Y position in the atlas (pixels)
 * @width: Width of the region (pixels)
 * @height: Height of the region (pixels)
 *
 * Creates a new atlas region.
 *
 * Returns: (transfer full): A new #LrgAtlasRegion
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasRegion *lrg_atlas_region_new            (const gchar *name,
                                                 gint         x,
                                                 gint         y,
                                                 gint         width,
                                                 gint         height);

/**
 * lrg_atlas_region_new_with_uv:
 * @name: Region name/identifier
 * @x: X position in the atlas (pixels)
 * @y: Y position in the atlas (pixels)
 * @width: Width of the region (pixels)
 * @height: Height of the region (pixels)
 * @u1: Left UV coordinate (0.0-1.0)
 * @v1: Top UV coordinate (0.0-1.0)
 * @u2: Right UV coordinate (0.0-1.0)
 * @v2: Bottom UV coordinate (0.0-1.0)
 *
 * Creates a new atlas region with explicit UV coordinates.
 *
 * Returns: (transfer full): A new #LrgAtlasRegion
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasRegion *lrg_atlas_region_new_with_uv    (const gchar *name,
                                                 gint         x,
                                                 gint         y,
                                                 gint         width,
                                                 gint         height,
                                                 gfloat       u1,
                                                 gfloat       v1,
                                                 gfloat       u2,
                                                 gfloat       v2);

/**
 * lrg_atlas_region_copy:
 * @self: A #LrgAtlasRegion
 *
 * Creates a copy of an atlas region.
 *
 * Returns: (transfer full): A copy of the region
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasRegion *lrg_atlas_region_copy           (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_free:
 * @self: A #LrgAtlasRegion
 *
 * Frees an atlas region.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_atlas_region_free           (LrgAtlasRegion *self);

/* Accessors */

/**
 * lrg_atlas_region_get_name:
 * @self: A #LrgAtlasRegion
 *
 * Gets the name of the region.
 *
 * Returns: (transfer none) (nullable): The region name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *   lrg_atlas_region_get_name       (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_get_x:
 * @self: A #LrgAtlasRegion
 *
 * Gets the X position in the atlas.
 *
 * Returns: X position in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_atlas_region_get_x          (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_get_y:
 * @self: A #LrgAtlasRegion
 *
 * Gets the Y position in the atlas.
 *
 * Returns: Y position in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_atlas_region_get_y          (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_get_width:
 * @self: A #LrgAtlasRegion
 *
 * Gets the width of the region.
 *
 * Returns: Width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_atlas_region_get_width      (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_get_height:
 * @self: A #LrgAtlasRegion
 *
 * Gets the height of the region.
 *
 * Returns: Height in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint            lrg_atlas_region_get_height     (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_get_rect:
 * @self: A #LrgAtlasRegion
 * @out_x: (out) (nullable): Return location for X
 * @out_y: (out) (nullable): Return location for Y
 * @out_width: (out) (nullable): Return location for width
 * @out_height: (out) (nullable): Return location for height
 *
 * Gets the full rectangle of the region.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_atlas_region_get_rect       (const LrgAtlasRegion *self,
                                                 gint                 *out_x,
                                                 gint                 *out_y,
                                                 gint                 *out_width,
                                                 gint                 *out_height);

/* UV coordinates */

/**
 * lrg_atlas_region_get_u1:
 * @self: A #LrgAtlasRegion
 *
 * Gets the left UV coordinate.
 *
 * Returns: U1 coordinate (0.0-1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_atlas_region_get_u1         (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_get_v1:
 * @self: A #LrgAtlasRegion
 *
 * Gets the top UV coordinate.
 *
 * Returns: V1 coordinate (0.0-1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_atlas_region_get_v1         (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_get_u2:
 * @self: A #LrgAtlasRegion
 *
 * Gets the right UV coordinate.
 *
 * Returns: U2 coordinate (0.0-1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_atlas_region_get_u2         (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_get_v2:
 * @self: A #LrgAtlasRegion
 *
 * Gets the bottom UV coordinate.
 *
 * Returns: V2 coordinate (0.0-1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_atlas_region_get_v2         (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_get_uv:
 * @self: A #LrgAtlasRegion
 * @out_u1: (out) (nullable): Return location for U1
 * @out_v1: (out) (nullable): Return location for V1
 * @out_u2: (out) (nullable): Return location for U2
 * @out_v2: (out) (nullable): Return location for V2
 *
 * Gets all UV coordinates.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_atlas_region_get_uv         (const LrgAtlasRegion *self,
                                                 gfloat               *out_u1,
                                                 gfloat               *out_v1,
                                                 gfloat               *out_u2,
                                                 gfloat               *out_v2);

/* Transform properties */

/**
 * lrg_atlas_region_is_rotated:
 * @self: A #LrgAtlasRegion
 *
 * Checks if the region is rotated 90 degrees.
 * Some atlas packers rotate sprites to save space.
 *
 * Returns: %TRUE if rotated
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_atlas_region_is_rotated     (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_set_rotated:
 * @self: A #LrgAtlasRegion
 * @rotated: Whether the region is rotated
 *
 * Sets whether the region is rotated.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_atlas_region_set_rotated    (LrgAtlasRegion *self,
                                                 gboolean        rotated);

/**
 * lrg_atlas_region_is_flipped_x:
 * @self: A #LrgAtlasRegion
 *
 * Checks if the region is flipped horizontally.
 *
 * Returns: %TRUE if flipped on X axis
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_atlas_region_is_flipped_x   (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_set_flipped_x:
 * @self: A #LrgAtlasRegion
 * @flipped: Whether to flip horizontally
 *
 * Sets horizontal flip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_atlas_region_set_flipped_x  (LrgAtlasRegion *self,
                                                 gboolean        flipped);

/**
 * lrg_atlas_region_is_flipped_y:
 * @self: A #LrgAtlasRegion
 *
 * Checks if the region is flipped vertically.
 *
 * Returns: %TRUE if flipped on Y axis
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean        lrg_atlas_region_is_flipped_y   (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_set_flipped_y:
 * @self: A #LrgAtlasRegion
 * @flipped: Whether to flip vertically
 *
 * Sets vertical flip.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_atlas_region_set_flipped_y  (LrgAtlasRegion *self,
                                                 gboolean        flipped);

/* Pivot/origin */

/**
 * lrg_atlas_region_get_pivot_x:
 * @self: A #LrgAtlasRegion
 *
 * Gets the pivot X offset (for rotation/positioning).
 *
 * Returns: Pivot X in pixels (0 = left edge)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_atlas_region_get_pivot_x    (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_get_pivot_y:
 * @self: A #LrgAtlasRegion
 *
 * Gets the pivot Y offset.
 *
 * Returns: Pivot Y in pixels (0 = top edge)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat          lrg_atlas_region_get_pivot_y    (const LrgAtlasRegion *self);

/**
 * lrg_atlas_region_set_pivot:
 * @self: A #LrgAtlasRegion
 * @x: Pivot X offset
 * @y: Pivot Y offset
 *
 * Sets the pivot point.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_atlas_region_set_pivot      (LrgAtlasRegion *self,
                                                 gfloat          x,
                                                 gfloat          y);

/* Utility */

/**
 * lrg_atlas_region_calculate_uv:
 * @self: A #LrgAtlasRegion
 * @atlas_width: Total width of the atlas texture
 * @atlas_height: Total height of the atlas texture
 *
 * Calculates and updates UV coordinates based on pixel position
 * and atlas dimensions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void            lrg_atlas_region_calculate_uv   (LrgAtlasRegion *self,
                                                 gint            atlas_width,
                                                 gint            atlas_height);

G_END_DECLS
