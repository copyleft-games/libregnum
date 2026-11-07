/* lrg-anim-graph.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAnimGraph: (stub, being written)
 */

#include "config.h"

#include "lrg-anim-graph.h"

struct _LrgAnimGraph
{
	GObject parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgAnimGraph, lrg_anim_graph, G_TYPE_OBJECT)

static void
lrg_anim_graph_class_init (LrgAnimGraphClass *klass)
{
}

static void
lrg_anim_graph_init (LrgAnimGraph *self)
{
}

LrgAnimGraph *
lrg_anim_graph_new (void)
{
	return g_object_new (LRG_TYPE_ANIM_GRAPH, NULL);
}
