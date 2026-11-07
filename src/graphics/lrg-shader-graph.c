/* lrg-shader-graph.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgShaderGraph: (stub, being written)
 */

#include "config.h"

#include "lrg-shader-graph.h"

struct _LrgShaderGraph
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgShaderGraph, lrg_shader_graph, G_TYPE_OBJECT)

static void
lrg_shader_graph_class_init (LrgShaderGraphClass *klass)
{
}

static void
lrg_shader_graph_init (LrgShaderGraph *self)
{
}

LrgShaderGraph *
lrg_shader_graph_new (void)
{
	return g_object_new (LRG_TYPE_SHADER_GRAPH, NULL);
}
