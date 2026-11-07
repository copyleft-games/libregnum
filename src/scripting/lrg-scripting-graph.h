/* lrg-scripting-graph.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptingGraph: (stub, being written)
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"


G_BEGIN_DECLS

#define LRG_TYPE_SCRIPTING_GRAPH (lrg_scripting_graph_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScriptingGraph, lrg_scripting_graph, LRG, SCRIPTING_GRAPH, GObject)

/**
 * lrg_scripting_graph_new:
 *
 * Returns: (transfer full): a new #LrgScriptingGraph
 */
LRG_AVAILABLE_IN_ALL
LrgScriptingGraph * lrg_scripting_graph_new (void);

G_END_DECLS
