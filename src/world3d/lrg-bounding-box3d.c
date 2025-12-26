/* lrg-bounding-box3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Axis-aligned bounding box (AABB) implementation.
 */

#include "config.h"
#include "lrg-bounding-box3d.h"

#include <math.h>

/* Register as a GBoxed type */
G_DEFINE_BOXED_TYPE (LrgBoundingBox3D, lrg_bounding_box3d,
                     lrg_bounding_box3d_copy, lrg_bounding_box3d_free)

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
LrgBoundingBox3D *
lrg_bounding_box3d_new (gfloat min_x,
                        gfloat min_y,
                        gfloat min_z,
                        gfloat max_x,
                        gfloat max_y,
                        gfloat max_z)
{
    LrgBoundingBox3D *self;

    self = g_new0 (LrgBoundingBox3D, 1);
    self->min.x = min_x;
    self->min.y = min_y;
    self->min.z = min_z;
    self->max.x = max_x;
    self->max.y = max_y;
    self->max.z = max_z;

    return self;
}

/**
 * lrg_bounding_box3d_new_from_vectors:
 * @min: (transfer none): Minimum corner
 * @max: (transfer none): Maximum corner
 *
 * Creates a new bounding box from two vectors.
 *
 * Returns: (transfer full): A newly allocated #LrgBoundingBox3D
 */
LrgBoundingBox3D *
lrg_bounding_box3d_new_from_vectors (const GrlVector3 *min,
                                     const GrlVector3 *max)
{
    g_return_val_if_fail (min != NULL, NULL);
    g_return_val_if_fail (max != NULL, NULL);

    return lrg_bounding_box3d_new (min->x, min->y, min->z,
                                   max->x, max->y, max->z);
}

/**
 * lrg_bounding_box3d_new_from_center:
 * @center: (transfer none): Center point
 * @half_size: Half the size in each dimension
 *
 * Creates a new bounding box centered at a point with uniform half-size.
 *
 * Returns: (transfer full): A newly allocated #LrgBoundingBox3D
 */
LrgBoundingBox3D *
lrg_bounding_box3d_new_from_center (const GrlVector3 *center,
                                    gfloat            half_size)
{
    g_return_val_if_fail (center != NULL, NULL);

    return lrg_bounding_box3d_new (center->x - half_size,
                                   center->y - half_size,
                                   center->z - half_size,
                                   center->x + half_size,
                                   center->y + half_size,
                                   center->z + half_size);
}

/**
 * lrg_bounding_box3d_copy:
 * @self: (nullable): A #LrgBoundingBox3D
 *
 * Creates a copy of the bounding box.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL if @self is %NULL
 */
LrgBoundingBox3D *
lrg_bounding_box3d_copy (const LrgBoundingBox3D *self)
{
    if (self == NULL)
        return NULL;

    return lrg_bounding_box3d_new (self->min.x, self->min.y, self->min.z,
                                   self->max.x, self->max.y, self->max.z);
}

/**
 * lrg_bounding_box3d_free:
 * @self: (nullable): A #LrgBoundingBox3D
 *
 * Frees a bounding box allocated with lrg_bounding_box3d_new() or related functions.
 */
void
lrg_bounding_box3d_free (LrgBoundingBox3D *self)
{
    if (self != NULL)
        g_free (self);
}

/**
 * lrg_bounding_box3d_get_min:
 * @self: A #LrgBoundingBox3D
 *
 * Gets the minimum corner of the bounding box.
 *
 * Returns: (transfer full): The minimum corner
 */
GrlVector3 *
lrg_bounding_box3d_get_min (const LrgBoundingBox3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return grl_vector3_new (self->min.x, self->min.y, self->min.z);
}

/**
 * lrg_bounding_box3d_get_max:
 * @self: A #LrgBoundingBox3D
 *
 * Gets the maximum corner of the bounding box.
 *
 * Returns: (transfer full): The maximum corner
 */
GrlVector3 *
lrg_bounding_box3d_get_max (const LrgBoundingBox3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return grl_vector3_new (self->max.x, self->max.y, self->max.z);
}

/**
 * lrg_bounding_box3d_get_center:
 * @self: A #LrgBoundingBox3D
 *
 * Gets the center point of the bounding box.
 *
 * Returns: (transfer full): The center point
 */
GrlVector3 *
lrg_bounding_box3d_get_center (const LrgBoundingBox3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return grl_vector3_new ((self->min.x + self->max.x) * 0.5f,
                            (self->min.y + self->max.y) * 0.5f,
                            (self->min.z + self->max.z) * 0.5f);
}

/**
 * lrg_bounding_box3d_get_size:
 * @self: A #LrgBoundingBox3D
 *
 * Gets the size (dimensions) of the bounding box.
 *
 * Returns: (transfer full): A vector with width, height, depth
 */
GrlVector3 *
lrg_bounding_box3d_get_size (const LrgBoundingBox3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return grl_vector3_new (self->max.x - self->min.x,
                            self->max.y - self->min.y,
                            self->max.z - self->min.z);
}

/**
 * lrg_bounding_box3d_contains_point:
 * @self: A #LrgBoundingBox3D
 * @point: (transfer none): The point to check
 *
 * Checks if a point is inside the bounding box.
 *
 * Returns: %TRUE if the point is inside
 */
gboolean
lrg_bounding_box3d_contains_point (const LrgBoundingBox3D *self,
                                   const GrlVector3       *point)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (point != NULL, FALSE);

    return lrg_bounding_box3d_contains_point_xyz (self, point->x, point->y, point->z);
}

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
gboolean
lrg_bounding_box3d_contains_point_xyz (const LrgBoundingBox3D *self,
                                       gfloat                  x,
                                       gfloat                  y,
                                       gfloat                  z)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return (x >= self->min.x && x <= self->max.x &&
            y >= self->min.y && y <= self->max.y &&
            z >= self->min.z && z <= self->max.z);
}

/**
 * lrg_bounding_box3d_intersects:
 * @self: A #LrgBoundingBox3D
 * @other: (transfer none): Another #LrgBoundingBox3D
 *
 * Checks if two bounding boxes intersect.
 *
 * Returns: %TRUE if they intersect
 */
gboolean
lrg_bounding_box3d_intersects (const LrgBoundingBox3D *self,
                               const LrgBoundingBox3D *other)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (other != NULL, FALSE);

    /* Two AABBs intersect if they overlap on all three axes */
    return (self->min.x <= other->max.x && self->max.x >= other->min.x &&
            self->min.y <= other->max.y && self->max.y >= other->min.y &&
            self->min.z <= other->max.z && self->max.z >= other->min.z);
}

/**
 * lrg_bounding_box3d_contains:
 * @self: A #LrgBoundingBox3D
 * @other: (transfer none): Another #LrgBoundingBox3D
 *
 * Checks if this bounding box fully contains another.
 *
 * Returns: %TRUE if @self fully contains @other
 */
gboolean
lrg_bounding_box3d_contains (const LrgBoundingBox3D *self,
                             const LrgBoundingBox3D *other)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (other != NULL, FALSE);

    return (self->min.x <= other->min.x && self->max.x >= other->max.x &&
            self->min.y <= other->min.y && self->max.y >= other->max.y &&
            self->min.z <= other->min.z && self->max.z >= other->max.z);
}

/**
 * lrg_bounding_box3d_expand:
 * @self: A #LrgBoundingBox3D
 * @amount: Amount to expand in all directions
 *
 * Creates a new bounding box expanded by the given amount in all directions.
 *
 * Returns: (transfer full): A new expanded bounding box
 */
LrgBoundingBox3D *
lrg_bounding_box3d_expand (const LrgBoundingBox3D *self,
                           gfloat                  amount)
{
    g_return_val_if_fail (self != NULL, NULL);

    return lrg_bounding_box3d_new (self->min.x - amount,
                                   self->min.y - amount,
                                   self->min.z - amount,
                                   self->max.x + amount,
                                   self->max.y + amount,
                                   self->max.z + amount);
}

/**
 * lrg_bounding_box3d_merge:
 * @self: A #LrgBoundingBox3D
 * @other: (transfer none): Another #LrgBoundingBox3D
 *
 * Creates a bounding box that contains both input boxes.
 *
 * Returns: (transfer full): A new merged bounding box
 */
LrgBoundingBox3D *
lrg_bounding_box3d_merge (const LrgBoundingBox3D *self,
                          const LrgBoundingBox3D *other)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (other != NULL, NULL);

    return lrg_bounding_box3d_new (MIN (self->min.x, other->min.x),
                                   MIN (self->min.y, other->min.y),
                                   MIN (self->min.z, other->min.z),
                                   MAX (self->max.x, other->max.x),
                                   MAX (self->max.y, other->max.y),
                                   MAX (self->max.z, other->max.z));
}

/**
 * lrg_bounding_box3d_get_volume:
 * @self: A #LrgBoundingBox3D
 *
 * Calculates the volume of the bounding box.
 *
 * Returns: The volume
 */
gfloat
lrg_bounding_box3d_get_volume (const LrgBoundingBox3D *self)
{
    gfloat width;
    gfloat height;
    gfloat depth;

    g_return_val_if_fail (self != NULL, 0.0f);

    width = self->max.x - self->min.x;
    height = self->max.y - self->min.y;
    depth = self->max.z - self->min.z;

    return width * height * depth;
}

/**
 * lrg_bounding_box3d_get_surface_area:
 * @self: A #LrgBoundingBox3D
 *
 * Calculates the surface area of the bounding box.
 *
 * Returns: The surface area
 */
gfloat
lrg_bounding_box3d_get_surface_area (const LrgBoundingBox3D *self)
{
    gfloat width;
    gfloat height;
    gfloat depth;

    g_return_val_if_fail (self != NULL, 0.0f);

    width = self->max.x - self->min.x;
    height = self->max.y - self->min.y;
    depth = self->max.z - self->min.z;

    /* Surface area = 2 * (w*h + h*d + d*w) */
    return 2.0f * (width * height + height * depth + depth * width);
}
