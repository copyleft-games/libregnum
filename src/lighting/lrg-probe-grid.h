/* lrg-probe-grid.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgProbeGrid: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_PROBE_GRID (lrg_probe_grid_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgProbeGrid, lrg_probe_grid, LRG, PROBE_GRID, GObject)

/**
 * lrg_probe_grid_new:
 *
 * Returns: (transfer full): a new #LrgProbeGrid
 */
LRG_AVAILABLE_IN_ALL
LrgProbeGrid * lrg_probe_grid_new (void);

G_END_DECLS
