/* lrg-sector-private.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Internal LrgSector API. Not installed, not introspected.
 */

#pragma once

#include "lrg-sector.h"

G_BEGIN_DECLS

/*
 * lrg_sector_peek_portal_ids:
 * @self: a #LrgSector
 *
 * The sector's own portal ID list, for traversals that only read it
 * (lrg_sector_get_portal_ids() returns a copy).
 *
 * Returns: (transfer none) (element-type utf8): the portal IDs
 */
G_GNUC_INTERNAL
const GPtrArray *lrg_sector_peek_portal_ids (const LrgSector *self);

/*
 * lrg_sector_peek_bounds:
 * @self: a #LrgSector
 *
 * The sector's own bounds (lrg_sector_get_bounds() returns a copy).
 *
 * Returns: (transfer none): the bounds
 */
G_GNUC_INTERNAL
const LrgBoundingBox3D *lrg_sector_peek_bounds (const LrgSector *self);

G_END_DECLS
