/* lrg-sector.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Sector type for portal-based visibility.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-bounding-box3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_SECTOR (lrg_sector_get_type ())

/**
 * LrgSector:
 *
 * A sector in a portal-based visibility system.
 *
 * Sectors define convex regions of space connected by portals.
 * The portal system uses sectors for occlusion culling.
 */
typedef struct _LrgSector LrgSector;

LRG_AVAILABLE_IN_ALL
GType               lrg_sector_get_type             (void) G_GNUC_CONST;

/**
 * lrg_sector_new:
 * @id: Unique identifier for this sector
 * @bounds: (transfer none): The sector bounds
 *
 * Creates a new sector.
 *
 * Returns: (transfer full): A newly allocated #LrgSector
 */
LRG_AVAILABLE_IN_ALL
LrgSector *         lrg_sector_new                  (const gchar            *id,
                                                     const LrgBoundingBox3D *bounds);

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
LRG_AVAILABLE_IN_ALL
LrgSector *         lrg_sector_new_box              (const gchar            *id,
                                                     gfloat                  min_x,
                                                     gfloat                  min_y,
                                                     gfloat                  min_z,
                                                     gfloat                  max_x,
                                                     gfloat                  max_y,
                                                     gfloat                  max_z);

/**
 * lrg_sector_copy:
 * @self: (nullable): A #LrgSector
 *
 * Creates a copy of the sector.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL if @self is %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgSector *         lrg_sector_copy                 (const LrgSector        *self);

/**
 * lrg_sector_free:
 * @self: (nullable): A #LrgSector
 *
 * Frees a sector.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sector_free                 (LrgSector              *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSector, lrg_sector_free)

/**
 * lrg_sector_get_id:
 * @self: A #LrgSector
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): The identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_sector_get_id               (const LrgSector        *self);

/**
 * lrg_sector_get_bounds:
 * @self: A #LrgSector
 *
 * Gets the sector bounds.
 *
 * Returns: (transfer full): The bounds
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_sector_get_bounds           (const LrgSector        *self);

/**
 * lrg_sector_add_portal:
 * @self: A #LrgSector
 * @portal_id: ID of portal to add
 *
 * Adds a portal connection to this sector.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sector_add_portal           (LrgSector              *self,
                                                     const gchar            *portal_id);

/**
 * lrg_sector_remove_portal:
 * @self: A #LrgSector
 * @portal_id: ID of portal to remove
 *
 * Removes a portal connection from this sector.
 *
 * Returns: %TRUE if the portal was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sector_remove_portal        (LrgSector              *self,
                                                     const gchar            *portal_id);

/**
 * lrg_sector_has_portal:
 * @self: A #LrgSector
 * @portal_id: Portal ID to check
 *
 * Checks if this sector has a specific portal.
 *
 * Returns: %TRUE if the portal is connected
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sector_has_portal           (const LrgSector        *self,
                                                     const gchar            *portal_id);

/**
 * lrg_sector_get_portal_ids:
 * @self: A #LrgSector
 *
 * Gets all connected portal IDs.
 *
 * Returns: (transfer container) (element-type utf8): List of portal IDs
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_sector_get_portal_ids       (const LrgSector        *self);

/**
 * lrg_sector_get_portal_count:
 * @self: A #LrgSector
 *
 * Gets the number of connected portals.
 *
 * Returns: Portal count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_sector_get_portal_count     (const LrgSector        *self);

/**
 * lrg_sector_contains_point:
 * @self: A #LrgSector
 * @point: (transfer none): Point to test
 *
 * Tests if a point is inside this sector.
 *
 * Returns: %TRUE if the point is inside
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sector_contains_point       (const LrgSector        *self,
                                                     const GrlVector3       *point);

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sector_contains_point_xyz   (const LrgSector        *self,
                                                     gfloat                  x,
                                                     gfloat                  y,
                                                     gfloat                  z);

/**
 * lrg_sector_is_visible:
 * @self: A #LrgSector
 *
 * Gets the visibility flag.
 *
 * Returns: %TRUE if sector is marked visible
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_sector_is_visible           (const LrgSector        *self);

/**
 * lrg_sector_set_visible:
 * @self: A #LrgSector
 * @visible: Visibility state
 *
 * Sets the visibility flag.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_sector_set_visible          (LrgSector              *self,
                                                     gboolean                visible);

/**
 * lrg_sector_get_center:
 * @self: A #LrgSector
 *
 * Gets the center point of the sector.
 *
 * Returns: (transfer full): The center point
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 *        lrg_sector_get_center           (const LrgSector        *self);

G_END_DECLS
