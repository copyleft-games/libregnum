/* lrg-portal.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Portal type for sector-based visibility.
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

#define LRG_TYPE_PORTAL (lrg_portal_get_type ())

/**
 * LrgPortal:
 *
 * A portal connecting two sectors in a 3D level.
 *
 * Portals define openings between sectors for visibility determination
 * in a portal-based occlusion culling system.
 */
typedef struct _LrgPortal LrgPortal;

LRG_AVAILABLE_IN_ALL
GType               lrg_portal_get_type             (void) G_GNUC_CONST;

/**
 * lrg_portal_new:
 * @id: Unique identifier for this portal
 * @bounds: (transfer none): The portal bounds (opening area)
 * @sector_a: ID of first connected sector
 * @sector_b: ID of second connected sector
 *
 * Creates a new portal connecting two sectors.
 *
 * Returns: (transfer full): A newly allocated #LrgPortal
 */
LRG_AVAILABLE_IN_ALL
LrgPortal *         lrg_portal_new                  (const gchar            *id,
                                                     const LrgBoundingBox3D *bounds,
                                                     const gchar            *sector_a,
                                                     const gchar            *sector_b);

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
LRG_AVAILABLE_IN_ALL
LrgPortal *         lrg_portal_new_with_normal      (const gchar            *id,
                                                     const LrgBoundingBox3D *bounds,
                                                     const gchar            *sector_a,
                                                     const gchar            *sector_b,
                                                     const GrlVector3       *normal);

/**
 * lrg_portal_copy:
 * @self: (nullable): A #LrgPortal
 *
 * Creates a copy of the portal.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL if @self is %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgPortal *         lrg_portal_copy                 (const LrgPortal        *self);

/**
 * lrg_portal_free:
 * @self: (nullable): A #LrgPortal
 *
 * Frees a portal.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_portal_free                 (LrgPortal              *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgPortal, lrg_portal_free)

/**
 * lrg_portal_get_id:
 * @self: A #LrgPortal
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): The identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_portal_get_id               (const LrgPortal        *self);

/**
 * lrg_portal_get_bounds:
 * @self: A #LrgPortal
 *
 * Gets the portal bounds.
 *
 * Returns: (transfer full): The bounds
 */
LRG_AVAILABLE_IN_ALL
LrgBoundingBox3D *  lrg_portal_get_bounds           (const LrgPortal        *self);

/**
 * lrg_portal_get_sector_a:
 * @self: A #LrgPortal
 *
 * Gets the first connected sector ID.
 *
 * Returns: (transfer none): The sector ID
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_portal_get_sector_a         (const LrgPortal        *self);

/**
 * lrg_portal_get_sector_b:
 * @self: A #LrgPortal
 *
 * Gets the second connected sector ID.
 *
 * Returns: (transfer none): The sector ID
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_portal_get_sector_b         (const LrgPortal        *self);

/**
 * lrg_portal_get_normal:
 * @self: A #LrgPortal
 *
 * Gets the portal normal (facing direction).
 *
 * Returns: (transfer full): The normal vector
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 *        lrg_portal_get_normal           (const LrgPortal        *self);

/**
 * lrg_portal_set_normal:
 * @self: A #LrgPortal
 * @normal: (transfer none): New normal direction
 *
 * Sets the portal normal direction.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_portal_set_normal           (LrgPortal              *self,
                                                     const GrlVector3       *normal);

/**
 * lrg_portal_get_other_sector:
 * @self: A #LrgPortal
 * @from_sector: The sector you're coming from
 *
 * Gets the sector on the other side of the portal.
 *
 * Returns: (transfer none) (nullable): The other sector ID, or %NULL if @from_sector is not connected
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_portal_get_other_sector     (const LrgPortal        *self,
                                                     const gchar            *from_sector);

/**
 * lrg_portal_is_visible_from:
 * @self: A #LrgPortal
 * @point: (transfer none): Point to check from
 *
 * Checks if the portal is visible from a point based on normal direction.
 *
 * Returns: %TRUE if the portal faces the point
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_portal_is_visible_from      (const LrgPortal        *self,
                                                     const GrlVector3       *point);

/**
 * lrg_portal_connects_sector:
 * @self: A #LrgPortal
 * @sector_id: Sector ID to check
 *
 * Checks if the portal connects to the given sector.
 *
 * Returns: %TRUE if the sector is connected
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_portal_connects_sector      (const LrgPortal        *self,
                                                     const gchar            *sector_id);

/**
 * lrg_portal_get_center:
 * @self: A #LrgPortal
 *
 * Gets the center point of the portal.
 *
 * Returns: (transfer full): The center point
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 *        lrg_portal_get_center           (const LrgPortal        *self);

G_END_DECLS
