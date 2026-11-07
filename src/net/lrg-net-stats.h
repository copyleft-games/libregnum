/* lrg-net-stats.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgNetStats: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_NET_STATS (lrg_net_stats_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgNetStats, lrg_net_stats, LRG, NET_STATS, GObject)

/**
 * lrg_net_stats_new:
 *
 * Returns: (transfer full): a new #LrgNetStats
 */
LRG_AVAILABLE_IN_ALL
LrgNetStats * lrg_net_stats_new (void);

G_END_DECLS
