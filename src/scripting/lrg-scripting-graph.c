/* lrg-scripting-graph.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScriptingGraph: (stub, being written)
 */

#include "config.h"

#include "lrg-scripting-graph.h"

struct _LrgScriptingGraph
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgScriptingGraph, lrg_scripting_graph, G_TYPE_OBJECT)

static void
lrg_scripting_graph_class_init (LrgScriptingGraphClass *klass)
{
}

static void
lrg_scripting_graph_init (LrgScriptingGraph *self)
{
}

LrgScriptingGraph *
lrg_scripting_graph_new (void)
{
	return g_object_new (LRG_TYPE_SCRIPTING_GRAPH, NULL);
}
