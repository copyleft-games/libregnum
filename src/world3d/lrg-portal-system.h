/* lrg-portal-system.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portal-based visibility system.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-portal.h"
#include "lrg-sector.h"

G_BEGIN_DECLS

#define LRG_TYPE_PORTAL_SYSTEM (lrg_portal_system_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPortalSystem, lrg_portal_system, LRG, PORTAL_SYSTEM, GObject)

/**
 * lrg_portal_system_new:
 *
 * Creates a new portal system.
 *
 * Returns: (transfer full): A new #LrgPortalSystem
 */
LRG_AVAILABLE_IN_ALL
LrgPortalSystem *   lrg_portal_system_new           (void);

/* --- Sector Management --- */

/**
 * lrg_portal_system_add_sector:
 * @self: An #LrgPortalSystem
 * @sector: (transfer none): Sector to add
 *
 * Adds a sector to the system.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_portal_system_add_sector    (LrgPortalSystem        *self,
                                                     const LrgSector        *sector);

/**
 * lrg_portal_system_remove_sector:
 * @self: An #LrgPortalSystem
 * @id: Sector ID to remove
 *
 * Removes a sector from the system.
 *
 * Returns: %TRUE if the sector was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_portal_system_remove_sector (LrgPortalSystem        *self,
                                                     const gchar            *id);

/**
 * lrg_portal_system_get_sector:
 * @self: An #LrgPortalSystem
 * @id: Sector ID
 *
 * Gets a sector by ID.
 *
 * Returns: (transfer none) (nullable): The sector, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
const LrgSector *   lrg_portal_system_get_sector    (LrgPortalSystem        *self,
                                                     const gchar            *id);

/**
 * lrg_portal_system_get_sectors:
 * @self: An #LrgPortalSystem
 *
 * Gets all sectors.
 *
 * Returns: (transfer container) (element-type LrgSector): Array of sectors
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_portal_system_get_sectors   (LrgPortalSystem        *self);

/**
 * lrg_portal_system_get_sector_count:
 * @self: An #LrgPortalSystem
 *
 * Gets the number of sectors.
 *
 * Returns: Sector count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_portal_system_get_sector_count (LrgPortalSystem     *self);

/**
 * lrg_portal_system_find_sector_at:
 * @self: An #LrgPortalSystem
 * @point: (transfer none): Point to test
 *
 * Finds the sector containing a point.
 *
 * Returns: (transfer none) (nullable): The sector, or %NULL if none contains the point
 */
LRG_AVAILABLE_IN_ALL
const LrgSector *   lrg_portal_system_find_sector_at (LrgPortalSystem       *self,
                                                      const GrlVector3      *point);

/* --- Portal Management --- */

/**
 * lrg_portal_system_add_portal:
 * @self: An #LrgPortalSystem
 * @portal: (transfer none): Portal to add
 *
 * Adds a portal to the system.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_portal_system_add_portal    (LrgPortalSystem        *self,
                                                     const LrgPortal        *portal);

/**
 * lrg_portal_system_remove_portal:
 * @self: An #LrgPortalSystem
 * @id: Portal ID to remove
 *
 * Removes a portal from the system.
 *
 * Returns: %TRUE if the portal was found and removed
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_portal_system_remove_portal (LrgPortalSystem        *self,
                                                     const gchar            *id);

/**
 * lrg_portal_system_get_portal:
 * @self: An #LrgPortalSystem
 * @id: Portal ID
 *
 * Gets a portal by ID.
 *
 * Returns: (transfer none) (nullable): The portal, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
const LrgPortal *   lrg_portal_system_get_portal    (LrgPortalSystem        *self,
                                                     const gchar            *id);

/**
 * lrg_portal_system_get_portals:
 * @self: An #LrgPortalSystem
 *
 * Gets all portals.
 *
 * Returns: (transfer container) (element-type LrgPortal): Array of portals
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_portal_system_get_portals   (LrgPortalSystem        *self);

/**
 * lrg_portal_system_get_portal_count:
 * @self: An #LrgPortalSystem
 *
 * Gets the number of portals.
 *
 * Returns: Portal count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_portal_system_get_portal_count (LrgPortalSystem     *self);

/* --- Visibility Determination --- */

/**
 * lrg_portal_system_update:
 * @self: An #LrgPortalSystem
 * @camera_pos: (transfer none): Camera position
 *
 * Updates sector visibility based on camera position.
 * This traverses portals to determine which sectors are visible.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_portal_system_update        (LrgPortalSystem        *self,
                                                     const GrlVector3       *camera_pos);

/**
 * lrg_portal_system_get_visible_sectors:
 * @self: An #LrgPortalSystem
 *
 * Gets all currently visible sectors.
 *
 * Returns: (transfer container) (element-type LrgSector): Array of visible sectors
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_portal_system_get_visible_sectors (LrgPortalSystem  *self);

/**
 * lrg_portal_system_get_visible_sector_count:
 * @self: An #LrgPortalSystem
 *
 * Gets the number of currently visible sectors.
 *
 * Returns: Visible sector count
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_portal_system_get_visible_sector_count (LrgPortalSystem *self);

/**
 * lrg_portal_system_is_sector_visible:
 * @self: An #LrgPortalSystem
 * @id: Sector ID to check
 *
 * Checks if a sector is currently visible.
 *
 * Returns: %TRUE if the sector is visible
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_portal_system_is_sector_visible (LrgPortalSystem    *self,
                                                          const gchar        *id);

/**
 * lrg_portal_system_get_current_sector:
 * @self: An #LrgPortalSystem
 *
 * Gets the sector the camera is currently in.
 *
 * Returns: (transfer none) (nullable): The current sector ID
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_portal_system_get_current_sector (LrgPortalSystem   *self);

/* --- Configuration --- */

/**
 * lrg_portal_system_get_max_portal_depth:
 * @self: An #LrgPortalSystem
 *
 * Gets the maximum portal traversal depth.
 *
 * Returns: Maximum depth
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_portal_system_get_max_portal_depth (LrgPortalSystem *self);

/**
 * lrg_portal_system_set_max_portal_depth:
 * @self: An #LrgPortalSystem
 * @max_depth: Maximum traversal depth
 *
 * Sets the maximum portal traversal depth.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_portal_system_set_max_portal_depth (LrgPortalSystem *self,
                                                             guint            max_depth);

/* --- Utility --- */

/**
 * lrg_portal_system_clear:
 * @self: An #LrgPortalSystem
 *
 * Removes all sectors and portals.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_portal_system_clear         (LrgPortalSystem        *self);

/**
 * lrg_portal_system_get_sector_portals:
 * @self: An #LrgPortalSystem
 * @sector_id: Sector ID
 *
 * Gets all portals connected to a sector.
 *
 * Returns: (transfer container) (element-type LrgPortal): Array of portals
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_portal_system_get_sector_portals (LrgPortalSystem   *self,
                                                           const gchar       *sector_id);

G_END_DECLS
