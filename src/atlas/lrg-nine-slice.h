/* lrg-nine-slice.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Nine-slice (9-patch) rendering for UI elements.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-atlas-region.h"

G_BEGIN_DECLS

#define LRG_TYPE_NINE_SLICE (lrg_nine_slice_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNineSlice, lrg_nine_slice, LRG, NINE_SLICE, GObject)

/**
 * lrg_nine_slice_new:
 * @name: Name identifier for the nine-slice
 *
 * Creates a new nine-slice without any source region.
 *
 * Returns: (transfer full): A new #LrgNineSlice
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgNineSlice *      lrg_nine_slice_new                      (const gchar *name);

/**
 * lrg_nine_slice_new_from_region:
 * @name: Name identifier
 * @region: Source atlas region
 * @left: Left border width in pixels
 * @right: Right border width in pixels
 * @top: Top border height in pixels
 * @bottom: Bottom border height in pixels
 *
 * Creates a nine-slice from an atlas region with specified borders.
 *
 * Returns: (transfer full): A new #LrgNineSlice
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgNineSlice *      lrg_nine_slice_new_from_region          (const gchar          *name,
                                                             const LrgAtlasRegion *region,
                                                             gint                  left,
                                                             gint                  right,
                                                             gint                  top,
                                                             gint                  bottom);

/**
 * lrg_nine_slice_new_from_file:
 * @path: Path to the nine-slice definition file (YAML)
 * @error: (nullable): Return location for error
 *
 * Creates a nine-slice by loading a definition file.
 *
 * Returns: (transfer full) (nullable): A new #LrgNineSlice or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgNineSlice *      lrg_nine_slice_new_from_file            (const gchar  *path,
                                                             GError      **error);

/* Properties */

/**
 * lrg_nine_slice_get_name:
 * @self: A #LrgNineSlice
 *
 * Gets the name of the nine-slice.
 *
 * Returns: (transfer none) (nullable): The name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_nine_slice_get_name                 (LrgNineSlice *self);

/**
 * lrg_nine_slice_get_source_region:
 * @self: A #LrgNineSlice
 *
 * Gets the source atlas region.
 *
 * Returns: (transfer none) (nullable): The source region
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgAtlasRegion * lrg_nine_slice_get_source_region     (LrgNineSlice *self);

/**
 * lrg_nine_slice_set_source_region:
 * @self: A #LrgNineSlice
 * @region: The source region (copied)
 *
 * Sets the source atlas region.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_nine_slice_set_source_region        (LrgNineSlice         *self,
                                                             const LrgAtlasRegion *region);

/**
 * lrg_nine_slice_get_mode:
 * @self: A #LrgNineSlice
 *
 * Gets the center/edge fill mode.
 *
 * Returns: The fill mode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgNineSliceMode    lrg_nine_slice_get_mode                 (LrgNineSlice *self);

/**
 * lrg_nine_slice_set_mode:
 * @self: A #LrgNineSlice
 * @mode: The fill mode
 *
 * Sets the center/edge fill mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_nine_slice_set_mode                 (LrgNineSlice    *self,
                                                             LrgNineSliceMode mode);

/* Border accessors */

/**
 * lrg_nine_slice_get_border_left:
 * @self: A #LrgNineSlice
 *
 * Gets the left border width.
 *
 * Returns: Left border in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_nine_slice_get_border_left          (LrgNineSlice *self);

/**
 * lrg_nine_slice_get_border_right:
 * @self: A #LrgNineSlice
 *
 * Gets the right border width.
 *
 * Returns: Right border in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_nine_slice_get_border_right         (LrgNineSlice *self);

/**
 * lrg_nine_slice_get_border_top:
 * @self: A #LrgNineSlice
 *
 * Gets the top border height.
 *
 * Returns: Top border in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_nine_slice_get_border_top           (LrgNineSlice *self);

/**
 * lrg_nine_slice_get_border_bottom:
 * @self: A #LrgNineSlice
 *
 * Gets the bottom border height.
 *
 * Returns: Bottom border in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_nine_slice_get_border_bottom        (LrgNineSlice *self);

/**
 * lrg_nine_slice_get_borders:
 * @self: A #LrgNineSlice
 * @out_left: (out) (nullable): Return location for left border
 * @out_right: (out) (nullable): Return location for right border
 * @out_top: (out) (nullable): Return location for top border
 * @out_bottom: (out) (nullable): Return location for bottom border
 *
 * Gets all border values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_nine_slice_get_borders              (LrgNineSlice *self,
                                                             gint         *out_left,
                                                             gint         *out_right,
                                                             gint         *out_top,
                                                             gint         *out_bottom);

/**
 * lrg_nine_slice_set_borders:
 * @self: A #LrgNineSlice
 * @left: Left border width
 * @right: Right border width
 * @top: Top border height
 * @bottom: Bottom border height
 *
 * Sets all border values.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_nine_slice_set_borders              (LrgNineSlice *self,
                                                             gint          left,
                                                             gint          right,
                                                             gint          top,
                                                             gint          bottom);

/**
 * lrg_nine_slice_set_uniform_border:
 * @self: A #LrgNineSlice
 * @border: Border size for all sides
 *
 * Sets all borders to the same value.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_nine_slice_set_uniform_border       (LrgNineSlice *self,
                                                             gint          border);

/* Size constraints */

/**
 * lrg_nine_slice_get_min_width:
 * @self: A #LrgNineSlice
 *
 * Gets the minimum width (left + right borders).
 *
 * Returns: Minimum width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_nine_slice_get_min_width            (LrgNineSlice *self);

/**
 * lrg_nine_slice_get_min_height:
 * @self: A #LrgNineSlice
 *
 * Gets the minimum height (top + bottom borders).
 *
 * Returns: Minimum height in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_nine_slice_get_min_height           (LrgNineSlice *self);

/**
 * lrg_nine_slice_get_center_width:
 * @self: A #LrgNineSlice
 *
 * Gets the width of the stretchable center region.
 *
 * Returns: Center width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_nine_slice_get_center_width         (LrgNineSlice *self);

/**
 * lrg_nine_slice_get_center_height:
 * @self: A #LrgNineSlice
 *
 * Gets the height of the stretchable center region.
 *
 * Returns: Center height in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_nine_slice_get_center_height        (LrgNineSlice *self);

/* Slice access */

/**
 * LrgNineSlicePatch:
 *
 * Enum identifying the nine patches.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_NINE_SLICE_PATCH_TOP_LEFT,
    LRG_NINE_SLICE_PATCH_TOP,
    LRG_NINE_SLICE_PATCH_TOP_RIGHT,
    LRG_NINE_SLICE_PATCH_LEFT,
    LRG_NINE_SLICE_PATCH_CENTER,
    LRG_NINE_SLICE_PATCH_RIGHT,
    LRG_NINE_SLICE_PATCH_BOTTOM_LEFT,
    LRG_NINE_SLICE_PATCH_BOTTOM,
    LRG_NINE_SLICE_PATCH_BOTTOM_RIGHT
} LrgNineSlicePatch;

/**
 * lrg_nine_slice_get_patch_region:
 * @self: A #LrgNineSlice
 * @patch: Which patch to get
 *
 * Gets the atlas region for a specific patch.
 * The returned region has correct UV coordinates for the patch.
 *
 * Returns: (transfer full) (nullable): A new region for the patch, or %NULL if invalid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgAtlasRegion *    lrg_nine_slice_get_patch_region         (LrgNineSlice     *self,
                                                             LrgNineSlicePatch patch);

/**
 * lrg_nine_slice_get_patch_rect:
 * @self: A #LrgNineSlice
 * @patch: Which patch to get
 * @out_x: (out) (nullable): Return location for X position
 * @out_y: (out) (nullable): Return location for Y position
 * @out_width: (out) (nullable): Return location for width
 * @out_height: (out) (nullable): Return location for height
 *
 * Gets the source rectangle for a specific patch within the source region.
 *
 * Returns: %TRUE if the patch rectangle was retrieved
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_nine_slice_get_patch_rect           (LrgNineSlice     *self,
                                                             LrgNineSlicePatch patch,
                                                             gint             *out_x,
                                                             gint             *out_y,
                                                             gint             *out_width,
                                                             gint             *out_height);

/* Rendering helpers */

/**
 * lrg_nine_slice_calculate_dest_rects:
 * @self: A #LrgNineSlice
 * @dest_x: Destination X position
 * @dest_y: Destination Y position
 * @dest_width: Destination width
 * @dest_height: Destination height
 * @out_rects: (array fixed-size=9) (out): Array of 9 destination rectangles (x,y,w,h for each)
 *
 * Calculates destination rectangles for rendering all 9 patches.
 * Each rectangle is 4 floats: x, y, width, height (36 floats total).
 * The order matches #LrgNineSlicePatch enumeration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_nine_slice_calculate_dest_rects     (LrgNineSlice *self,
                                                             gfloat        dest_x,
                                                             gfloat        dest_y,
                                                             gfloat        dest_width,
                                                             gfloat        dest_height,
                                                             gfloat       *out_rects);

/**
 * lrg_nine_slice_calculate_tile_count:
 * @self: A #LrgNineSlice
 * @dest_width: Destination width
 * @dest_height: Destination height
 * @out_h_tiles: (out) (nullable): Return location for horizontal tile count
 * @out_v_tiles: (out) (nullable): Return location for vertical tile count
 *
 * Calculates how many tiles are needed for tiling modes.
 * Only applicable when mode is TILE or TILE_FIT.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_nine_slice_calculate_tile_count     (LrgNineSlice *self,
                                                             gfloat        dest_width,
                                                             gfloat        dest_height,
                                                             guint        *out_h_tiles,
                                                             guint        *out_v_tiles);

/* Serialization */

/**
 * lrg_nine_slice_save_to_file:
 * @self: A #LrgNineSlice
 * @path: Path to save to
 * @error: (nullable): Return location for error
 *
 * Saves the nine-slice definition to a YAML file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_nine_slice_save_to_file             (LrgNineSlice  *self,
                                                             const gchar   *path,
                                                             GError       **error);

G_END_DECLS
