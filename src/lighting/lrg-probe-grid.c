/* lrg-probe-grid.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgProbeGrid: (stub, being written)
 */

#include "config.h"

#include "lrg-probe-grid.h"

struct _LrgProbeGrid
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgProbeGrid, lrg_probe_grid, G_TYPE_OBJECT)

static void
lrg_probe_grid_class_init (LrgProbeGridClass *klass)
{
}

static void
lrg_probe_grid_init (LrgProbeGrid *self)
{
}

LrgProbeGrid *
lrg_probe_grid_new (void)
{
	return g_object_new (LRG_TYPE_PROBE_GRID, NULL);
}
