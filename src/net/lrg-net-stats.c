/* lrg-net-stats.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNetStats: (stub, being written)
 */

#include "config.h"

#include "lrg-net-stats.h"

struct _LrgNetStats
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgNetStats, lrg_net_stats, G_TYPE_OBJECT)

static void
lrg_net_stats_class_init (LrgNetStatsClass *klass)
{
}

static void
lrg_net_stats_init (LrgNetStats *self)
{
}

LrgNetStats *
lrg_net_stats_new (void)
{
	return g_object_new (LRG_TYPE_NET_STATS, NULL);
}
