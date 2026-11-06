/* lrg-tree-view.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTreeView: A virtualized expandable tree of rows.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-tree-view.h"
#include "../lrg-log.h"

struct _LrgTreeView
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgTreeView, lrg_tree_view, LRG_TYPE_CONTAINER)

static void
lrg_tree_view_class_init (LrgTreeViewClass *klass)
{
}

static void
lrg_tree_view_init (LrgTreeView *self)
{
}

LrgTreeView *
lrg_tree_view_new (void)
{
	return g_object_new (LRG_TYPE_TREE_VIEW, NULL);
}
