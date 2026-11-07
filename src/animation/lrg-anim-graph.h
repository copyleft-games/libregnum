/* lrg-anim-graph.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnimGraph: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_ANIM_GRAPH (lrg_anim_graph_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgAnimGraph, lrg_anim_graph, LRG, ANIM_GRAPH, GObject)

/**
 * lrg_anim_graph_new:
 *
 * Returns: (transfer full): a new #LrgAnimGraph
 */
LRG_AVAILABLE_IN_ALL
LrgAnimGraph * lrg_anim_graph_new (void);

G_END_DECLS
