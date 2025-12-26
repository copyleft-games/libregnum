/* lrg-bounding-box3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Axis-aligned bounding box (AABB) type for 3D world module.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_BOUNDING_BOX3D (lrg_bounding_box3d_get_type ())

/**
 * LrgBoundingBox3D:
 *
 * An axis-aligned bounding box (AABB) in 3D space.
 *
 * Used for spatial queries, collision volumes, and level geometry.
 */
typedef struct _LrgBoundingBox3D LrgBoundingBox3D;

struct _LrgBoundingBox3D
{
    /*< public >*/
    GrlVector3 min;
    GrlVector3 max;
};

LRG_AVAILABLE_IN_ALL
GType               lrg_bounding_box3d_get_type         (void) G_GNUC_CONST;

/**
 * lrg_bounding_box3d_new:
 * @min_x: Minimum X coordinate
 * @min_y: Minimum Y coordinate
 * @min_z: Minimum Z coordinate
 * @max_x: Maximum X coordinate
 * @max_y: Maximum Y coordinate
 * @max_z: Maximum Z coordinate
 *
 * Creates a new bounding box from individual coordinates.
 *
 * Returns: (transfer full): A newly allocated #LrgBoundingBox3D
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_bounding_box3d_new              (gfloat              min_x,
                                                         gfloat              min_y,
                                                         gfloat              min_z,
                                                         gfloat              max_x,
                                                         gfloat              max_y,
                                                         gfloat              max_z);

/**
 * lrg_bounding_box3d_new_from_vectors:
 * @min: (transfer none): Minimum corner
 * @max: (transfer none): Maximum corner
 *
 * Creates a new bounding box from two vectors.
 *
 * Returns: (transfer full): A newly allocated #LrgBoundingBox3D
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_bounding_box3d_new_from_vectors (const GrlVector3   *min,
                                                         const GrlVector3   *max);

/**
 * lrg_bounding_box3d_new_from_center:
 * @center: (transfer none): Center point
 * @half_size: Half the size in each dimension
 *
 * Creates a new bounding box centered at a point with uniform half-size.
 *
 * Returns: (transfer full): A newly allocated #LrgBoundingBox3D
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_bounding_box3d_new_from_center  (const GrlVector3   *center,
                                                         gfloat              half_size);

/**
 * lrg_bounding_box3d_copy:
 * @self: (nullable): A #LrgBoundingBox3D
 *
 * Creates a copy of the bounding box.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL if @self is %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_bounding_box3d_copy             (const LrgBoundingBox3D *self);

/**
 * lrg_bounding_box3d_free:
 * @self: (nullable): A #LrgBoundingBox3D
 *
 * Frees a bounding box allocated with lrg_bounding_box3d_new() or related functions.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_bounding_box3d_free             (LrgBoundingBox3D   *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgBoundingBox3D, lrg_bounding_box3d_free)

/**
 * lrg_bounding_box3d_get_min:
 * @self: A #LrgBoundingBox3D
 *
 * Gets the minimum corner of the bounding box.
 *
 * Returns: (transfer full): The minimum corner
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 *        lrg_bounding_box3d_get_min          (const LrgBoundingBox3D *self);

/**
 * lrg_bounding_box3d_get_max:
 * @self: A #LrgBoundingBox3D
 *
 * Gets the maximum corner of the bounding box.
 *
 * Returns: (transfer full): The maximum corner
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 *        lrg_bounding_box3d_get_max          (const LrgBoundingBox3D *self);

/**
 * lrg_bounding_box3d_get_center:
 * @self: A #LrgBoundingBox3D
 *
 * Gets the center point of the bounding box.
 *
 * Returns: (transfer full): The center point
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 *        lrg_bounding_box3d_get_center       (const LrgBoundingBox3D *self);

/**
 * lrg_bounding_box3d_get_size:
 * @self: A #LrgBoundingBox3D
 *
 * Gets the size (dimensions) of the bounding box.
 *
 * Returns: (transfer full): A vector with width, height, depth
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 *        lrg_bounding_box3d_get_size         (const LrgBoundingBox3D *self);

/**
 * lrg_bounding_box3d_contains_point:
 * @self: A #LrgBoundingBox3D
 * @point: (transfer none): The point to check
 *
 * Checks if a point is inside the bounding box.
 *
 * Returns: %TRUE if the point is inside
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_bounding_box3d_contains_point   (const LrgBoundingBox3D *self,
                                                         const GrlVector3       *point);

/**
 * lrg_bounding_box3d_contains_point_xyz:
 * @self: A #LrgBoundingBox3D
 * @x: X coordinate
 * @y: Y coordinate
 * @z: Z coordinate
 *
 * Checks if a point (given as coordinates) is inside the bounding box.
 *
 * Returns: %TRUE if the point is inside
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_bounding_box3d_contains_point_xyz (const LrgBoundingBox3D *self,
                                                           gfloat                  x,
                                                           gfloat                  y,
                                                           gfloat                  z);

/**
 * lrg_bounding_box3d_intersects:
 * @self: A #LrgBoundingBox3D
 * @other: (transfer none): Another #LrgBoundingBox3D
 *
 * Checks if two bounding boxes intersect.
 *
 * Returns: %TRUE if they intersect
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_bounding_box3d_intersects       (const LrgBoundingBox3D *self,
                                                         const LrgBoundingBox3D *other);

/**
 * lrg_bounding_box3d_contains:
 * @self: A #LrgBoundingBox3D
 * @other: (transfer none): Another #LrgBoundingBox3D
 *
 * Checks if this bounding box fully contains another.
 *
 * Returns: %TRUE if @self fully contains @other
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_bounding_box3d_contains         (const LrgBoundingBox3D *self,
                                                         const LrgBoundingBox3D *other);

/**
 * lrg_bounding_box3d_expand:
 * @self: A #LrgBoundingBox3D
 * @amount: Amount to expand in all directions
 *
 * Creates a new bounding box expanded by the given amount in all directions.
 *
 * Returns: (transfer full): A new expanded bounding box
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_bounding_box3d_expand           (const LrgBoundingBox3D *self,
                                                         gfloat                  amount);

/**
 * lrg_bounding_box3d_merge:
 * @self: A #LrgBoundingBox3D
 * @other: (transfer none): Another #LrgBoundingBox3D
 *
 * Creates a bounding box that contains both input boxes.
 *
 * Returns: (transfer full): A new merged bounding box
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_bounding_box3d_merge            (const LrgBoundingBox3D *self,
                                                         const LrgBoundingBox3D *other);

/**
 * lrg_bounding_box3d_get_volume:
 * @self: A #LrgBoundingBox3D
 *
 * Calculates the volume of the bounding box.
 *
 * Returns: The volume
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_bounding_box3d_get_volume       (const LrgBoundingBox3D *self);

/**
 * lrg_bounding_box3d_get_surface_area:
 * @self: A #LrgBoundingBox3D
 *
 * Calculates the surface area of the bounding box.
 *
 * Returns: The surface area
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_bounding_box3d_get_surface_area (const LrgBoundingBox3D *self);

G_END_DECLS
