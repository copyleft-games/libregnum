/* lrg-sector.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sector implementation.
 */

#include "config.h"
#include "lrg-sector.h"

struct _LrgSector
{
    gchar           *id;
    LrgBoundingBox3D bounds;
    GPtrArray       *portal_ids;    /* gchar* */
    gboolean         visible;       /* Runtime visibility flag */
};

/* Register as a GBoxed type */
G_DEFINE_BOXED_TYPE (LrgSector, lrg_sector,
                     lrg_sector_copy, lrg_sector_free)

/**
 * lrg_sector_new:
 * @id: Unique identifier for this sector
 * @bounds: (transfer none): The sector bounds
 *
 * Creates a new sector.
 *
 * Returns: (transfer full): A newly allocated #LrgSector
 */
LrgSector *
lrg_sector_new (const gchar            *id,
                const LrgBoundingBox3D *bounds)
{
    LrgSector *self;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (bounds != NULL, NULL);

    self = g_new0 (LrgSector, 1);
    self->id = g_strdup (id);
    self->bounds = *bounds;
    self->portal_ids = g_ptr_array_new_with_free_func (g_free);
    self->visible = FALSE;

    return self;
}

/**
 * lrg_sector_new_box:
 * @id: Unique identifier for this sector
 * @min_x: Minimum X coordinate
 * @min_y: Minimum Y coordinate
 * @min_z: Minimum Z coordinate
 * @max_x: Maximum X coordinate
 * @max_y: Maximum Y coordinate
 * @max_z: Maximum Z coordinate
 *
 * Creates a new sector from box coordinates.
 *
 * Returns: (transfer full): A newly allocated #LrgSector
 */
LrgSector *
lrg_sector_new_box (const gchar *id,
                    gfloat       min_x,
                    gfloat       min_y,
                    gfloat       min_z,
                    gfloat       max_x,
                    gfloat       max_y,
                    gfloat       max_z)
{
    LrgSector *self;

    g_return_val_if_fail (id != NULL, NULL);

    self = g_new0 (LrgSector, 1);
    self->id = g_strdup (id);
    self->bounds.min.x = min_x;
    self->bounds.min.y = min_y;
    self->bounds.min.z = min_z;
    self->bounds.max.x = max_x;
    self->bounds.max.y = max_y;
    self->bounds.max.z = max_z;
    self->portal_ids = g_ptr_array_new_with_free_func (g_free);
    self->visible = FALSE;

    return self;
}

/**
 * lrg_sector_copy:
 * @self: (nullable): A #LrgSector
 *
 * Creates a copy of the sector.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL if @self is %NULL
 */
LrgSector *
lrg_sector_copy (const LrgSector *self)
{
    LrgSector *copy;
    guint i;

    if (self == NULL)
        return NULL;

    copy = lrg_sector_new (self->id, &self->bounds);
    copy->visible = self->visible;

    /* Copy portal IDs */
    for (i = 0; i < self->portal_ids->len; i++)
    {
        const gchar *portal_id = g_ptr_array_index (self->portal_ids, i);
        g_ptr_array_add (copy->portal_ids, g_strdup (portal_id));
    }

    return copy;
}

/**
 * lrg_sector_free:
 * @self: (nullable): A #LrgSector
 *
 * Frees a sector.
 */
void
lrg_sector_free (LrgSector *self)
{
    if (self == NULL)
        return;

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->portal_ids, g_ptr_array_unref);
    g_free (self);
}

/**
 * lrg_sector_get_id:
 * @self: A #LrgSector
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): The identifier
 */
const gchar *
lrg_sector_get_id (const LrgSector *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->id;
}

/**
 * lrg_sector_get_bounds:
 * @self: A #LrgSector
 *
 * Gets the sector bounds.
 *
 * Returns: (transfer full): The bounds
 */
LrgBoundingBox3D *
lrg_sector_get_bounds (const LrgSector *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return lrg_bounding_box3d_copy (&self->bounds);
}

/**
 * lrg_sector_add_portal:
 * @self: A #LrgSector
 * @portal_id: ID of portal to add
 *
 * Adds a portal connection to this sector.
 * Does nothing if the portal is already added.
 */
void
lrg_sector_add_portal (LrgSector   *self,
                       const gchar *portal_id)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (portal_id != NULL);

    /* Avoid duplicates */
    if (!lrg_sector_has_portal (self, portal_id))
        g_ptr_array_add (self->portal_ids, g_strdup (portal_id));
}

/**
 * lrg_sector_remove_portal:
 * @self: A #LrgSector
 * @portal_id: ID of portal to remove
 *
 * Removes a portal connection from this sector.
 *
 * Returns: %TRUE if the portal was found and removed
 */
gboolean
lrg_sector_remove_portal (LrgSector   *self,
                          const gchar *portal_id)
{
    guint i;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (portal_id != NULL, FALSE);

    for (i = 0; i < self->portal_ids->len; i++)
    {
        const gchar *id = g_ptr_array_index (self->portal_ids, i);
        if (g_strcmp0 (id, portal_id) == 0)
        {
            g_ptr_array_remove_index (self->portal_ids, i);
            return TRUE;
        }
    }

    return FALSE;
}

/**
 * lrg_sector_has_portal:
 * @self: A #LrgSector
 * @portal_id: Portal ID to check
 *
 * Checks if this sector has a specific portal.
 *
 * Returns: %TRUE if the portal is connected
 */
gboolean
lrg_sector_has_portal (const LrgSector *self,
                       const gchar     *portal_id)
{
    guint i;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (portal_id != NULL, FALSE);

    for (i = 0; i < self->portal_ids->len; i++)
    {
        const gchar *id = g_ptr_array_index (self->portal_ids, i);
        if (g_strcmp0 (id, portal_id) == 0)
            return TRUE;
    }

    return FALSE;
}

/**
 * lrg_sector_get_portal_ids:
 * @self: A #LrgSector
 *
 * Gets all connected portal IDs.
 *
 * Returns: (transfer container) (element-type utf8): List of portal IDs
 */
GPtrArray *
lrg_sector_get_portal_ids (const LrgSector *self)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (self != NULL, NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < self->portal_ids->len; i++)
    {
        const gchar *portal_id = g_ptr_array_index (self->portal_ids, i);
        g_ptr_array_add (result, (gpointer)portal_id);
    }

    return result;
}

/**
 * lrg_sector_get_portal_count:
 * @self: A #LrgSector
 *
 * Gets the number of connected portals.
 *
 * Returns: Portal count
 */
guint
lrg_sector_get_portal_count (const LrgSector *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->portal_ids->len;
}

/**
 * lrg_sector_contains_point:
 * @self: A #LrgSector
 * @point: (transfer none): Point to test
 *
 * Tests if a point is inside this sector.
 *
 * Returns: %TRUE if the point is inside
 */
gboolean
lrg_sector_contains_point (const LrgSector  *self,
                           const GrlVector3 *point)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (point != NULL, FALSE);

    return lrg_sector_contains_point_xyz (self, point->x, point->y, point->z);
}

/**
 * lrg_sector_contains_point_xyz:
 * @self: A #LrgSector
 * @x: X coordinate
 * @y: Y coordinate
 * @z: Z coordinate
 *
 * Tests if a point is inside this sector.
 *
 * Returns: %TRUE if the point is inside
 */
gboolean
lrg_sector_contains_point_xyz (const LrgSector *self,
                               gfloat           x,
                               gfloat           y,
                               gfloat           z)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return lrg_bounding_box3d_contains_point_xyz (&self->bounds, x, y, z);
}

/**
 * lrg_sector_is_visible:
 * @self: A #LrgSector
 *
 * Gets the visibility flag.
 *
 * Returns: %TRUE if sector is marked visible
 */
gboolean
lrg_sector_is_visible (const LrgSector *self)
{
    g_return_val_if_fail (self != NULL, FALSE);

    return self->visible;
}

/**
 * lrg_sector_set_visible:
 * @self: A #LrgSector
 * @visible: Visibility state
 *
 * Sets the visibility flag.
 */
void
lrg_sector_set_visible (LrgSector *self,
                        gboolean   visible)
{
    g_return_if_fail (self != NULL);

    self->visible = visible;
}

/**
 * lrg_sector_get_center:
 * @self: A #LrgSector
 *
 * Gets the center point of the sector.
 *
 * Returns: (transfer full): The center point
 */
GrlVector3 *
lrg_sector_get_center (const LrgSector *self)
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
