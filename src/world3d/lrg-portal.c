/* lrg-portal.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portal implementation.
 */

#include "config.h"
#include "lrg-portal.h"

#include <math.h>

struct _LrgPortal
{
    gchar           *id;
    LrgBoundingBox3D bounds;
    gchar           *sector_a;
    gchar           *sector_b;
    GrlVector3       normal;
};

/* Register as a GBoxed type */
G_DEFINE_BOXED_TYPE (LrgPortal, lrg_portal,
                     lrg_portal_copy, lrg_portal_free)

/*
 * compute_default_normal:
 *
 * Computes a default normal based on the portal bounds.
 * Uses the smallest dimension to determine facing direction.
 */
static void
compute_default_normal (const LrgBoundingBox3D *bounds,
                        GrlVector3             *normal)
{
    gfloat dx;
    gfloat dy;
    gfloat dz;

    dx = bounds->max.x - bounds->min.x;
    dy = bounds->max.y - bounds->min.y;
    dz = bounds->max.z - bounds->min.z;

    /* Normal points along the thinnest axis */
    if (dx <= dy && dx <= dz)
    {
        normal->x = 1.0f;
        normal->y = 0.0f;
        normal->z = 0.0f;
    }
    else if (dy <= dx && dy <= dz)
    {
        normal->x = 0.0f;
        normal->y = 1.0f;
        normal->z = 0.0f;
    }
    else
    {
        normal->x = 0.0f;
        normal->y = 0.0f;
        normal->z = 1.0f;
    }
}

/**
 * lrg_portal_new:
 * @id: Unique identifier for this portal
 * @bounds: (transfer none): The portal bounds (opening area)
 * @sector_a: ID of first connected sector
 * @sector_b: ID of second connected sector
 *
 * Creates a new portal connecting two sectors.
 * Normal is computed automatically from bounds.
 *
 * Returns: (transfer full): A newly allocated #LrgPortal
 */
LrgPortal *
lrg_portal_new (const gchar            *id,
                const LrgBoundingBox3D *bounds,
                const gchar            *sector_a,
                const gchar            *sector_b)
{
    LrgPortal *self;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (bounds != NULL, NULL);
    g_return_val_if_fail (sector_a != NULL, NULL);
    g_return_val_if_fail (sector_b != NULL, NULL);

    self = g_new0 (LrgPortal, 1);
    self->id = g_strdup (id);
    self->bounds = *bounds;
    self->sector_a = g_strdup (sector_a);
    self->sector_b = g_strdup (sector_b);

    compute_default_normal (bounds, &self->normal);

    return self;
}

/**
 * lrg_portal_new_with_normal:
 * @id: Unique identifier for this portal
 * @bounds: (transfer none): The portal bounds (opening area)
 * @sector_a: ID of first connected sector
 * @sector_b: ID of second connected sector
 * @normal: (transfer none): Portal facing direction
 *
 * Creates a new portal with explicit normal direction.
 *
 * Returns: (transfer full): A newly allocated #LrgPortal
 */
LrgPortal *
lrg_portal_new_with_normal (const gchar            *id,
                            const LrgBoundingBox3D *bounds,
                            const gchar            *sector_a,
                            const gchar            *sector_b,
                            const GrlVector3       *normal)
{
    LrgPortal *self;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (bounds != NULL, NULL);
    g_return_val_if_fail (sector_a != NULL, NULL);
    g_return_val_if_fail (sector_b != NULL, NULL);
    g_return_val_if_fail (normal != NULL, NULL);

    self = g_new0 (LrgPortal, 1);
    self->id = g_strdup (id);
    self->bounds = *bounds;
    self->sector_a = g_strdup (sector_a);
    self->sector_b = g_strdup (sector_b);
    self->normal = *normal;

    return self;
}

/**
 * lrg_portal_copy:
 * @self: (nullable): A #LrgPortal
 *
 * Creates a copy of the portal.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL if @self is %NULL
 */
LrgPortal *
lrg_portal_copy (const LrgPortal *self)
{
    LrgPortal *copy;

    if (self == NULL)
        return NULL;

    copy = g_new0 (LrgPortal, 1);
    copy->id = g_strdup (self->id);
    copy->bounds = self->bounds;
    copy->sector_a = g_strdup (self->sector_a);
    copy->sector_b = g_strdup (self->sector_b);
    copy->normal = self->normal;

    return copy;
}

/**
 * lrg_portal_free:
 * @self: (nullable): A #LrgPortal
 *
 * Frees a portal.
 */
void
lrg_portal_free (LrgPortal *self)
{
    if (self == NULL)
        return;

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->sector_a, g_free);
    g_clear_pointer (&self->sector_b, g_free);
    g_free (self);
}

/**
 * lrg_portal_get_id:
 * @self: A #LrgPortal
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): The identifier
 */
const gchar *
lrg_portal_get_id (const LrgPortal *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->id;
}

/**
 * lrg_portal_get_bounds:
 * @self: A #LrgPortal
 *
 * Gets the portal bounds.
 *
 * Returns: (transfer full): The bounds
 */
LrgBoundingBox3D *
lrg_portal_get_bounds (const LrgPortal *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return lrg_bounding_box3d_copy (&self->bounds);
}

/**
 * lrg_portal_get_sector_a:
 * @self: A #LrgPortal
 *
 * Gets the first connected sector ID.
 *
 * Returns: (transfer none): The sector ID
 */
const gchar *
lrg_portal_get_sector_a (const LrgPortal *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->sector_a;
}

/**
 * lrg_portal_get_sector_b:
 * @self: A #LrgPortal
 *
 * Gets the second connected sector ID.
 *
 * Returns: (transfer none): The sector ID
 */
const gchar *
lrg_portal_get_sector_b (const LrgPortal *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->sector_b;
}

/**
 * lrg_portal_get_normal:
 * @self: A #LrgPortal
 *
 * Gets the portal normal (facing direction).
 *
 * Returns: (transfer full): The normal vector
 */
GrlVector3 *
lrg_portal_get_normal (const LrgPortal *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return grl_vector3_new (self->normal.x, self->normal.y, self->normal.z);
}

/**
 * lrg_portal_set_normal:
 * @self: A #LrgPortal
 * @normal: (transfer none): New normal direction
 *
 * Sets the portal normal direction.
 */
void
lrg_portal_set_normal (LrgPortal        *self,
                       const GrlVector3 *normal)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (normal != NULL);

    self->normal.x = normal->x;
    self->normal.y = normal->y;
    self->normal.z = normal->z;
}

/**
 * lrg_portal_get_other_sector:
 * @self: A #LrgPortal
 * @from_sector: The sector you're coming from
 *
 * Gets the sector on the other side of the portal.
 *
 * Returns: (transfer none) (nullable): The other sector ID, or %NULL if @from_sector is not connected
 */
const gchar *
lrg_portal_get_other_sector (const LrgPortal *self,
                             const gchar     *from_sector)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (from_sector != NULL, NULL);

    if (g_strcmp0 (self->sector_a, from_sector) == 0)
        return self->sector_b;
    else if (g_strcmp0 (self->sector_b, from_sector) == 0)
        return self->sector_a;

    return NULL;
}

/**
 * lrg_portal_is_visible_from:
 * @self: A #LrgPortal
 * @point: (transfer none): Point to check from
 *
 * Checks if the portal is visible from a point based on normal direction.
 *
 * Returns: %TRUE if the portal faces the point
 */
gboolean
lrg_portal_is_visible_from (const LrgPortal  *self,
                            const GrlVector3 *point)
{
    gfloat center_x;
    gfloat center_y;
    gfloat center_z;
    gfloat to_point_x;
    gfloat to_point_y;
    gfloat to_point_z;
    gfloat dot;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (point != NULL, FALSE);

    /* Compute portal center */
    center_x = (self->bounds.min.x + self->bounds.max.x) * 0.5f;
    center_y = (self->bounds.min.y + self->bounds.max.y) * 0.5f;
    center_z = (self->bounds.min.z + self->bounds.max.z) * 0.5f;

    /* Vector from center to point */
    to_point_x = point->x - center_x;
    to_point_y = point->y - center_y;
    to_point_z = point->z - center_z;

    /* Dot product with normal */
    dot = to_point_x * self->normal.x +
          to_point_y * self->normal.y +
          to_point_z * self->normal.z;

    /* Portal is visible if point is on the positive side of normal */
    return dot > 0.0f;
}

/**
 * lrg_portal_connects_sector:
 * @self: A #LrgPortal
 * @sector_id: Sector ID to check
 *
 * Checks if the portal connects to the given sector.
 *
 * Returns: %TRUE if the sector is connected
 */
gboolean
lrg_portal_connects_sector (const LrgPortal *self,
                            const gchar     *sector_id)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (sector_id != NULL, FALSE);

    return g_strcmp0 (self->sector_a, sector_id) == 0 ||
           g_strcmp0 (self->sector_b, sector_id) == 0;
}

/**
 * lrg_portal_get_center:
 * @self: A #LrgPortal
 *
 * Gets the center point of the portal.
 *
 * Returns: (transfer full): The center point
 */
GrlVector3 *
lrg_portal_get_center (const LrgPortal *self)
{
    gfloat cx;
    gfloat cy;
    gfloat cz;

    g_return_val_if_fail (self != NULL, NULL);

    cx = (self->bounds.min.x + self->bounds.max.x) * 0.5f;
    cy = (self->bounds.min.y + self->bounds.max.y) * 0.5f;
    cz = (self->bounds.min.z + self->bounds.max.z) * 0.5f;

    return grl_vector3_new (cx, cy, cz);
}
