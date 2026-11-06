/* lrg-list-view.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgListView: A virtualized, scrollable list of rows built from a GListModel.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_UI

#include "lrg-list-view.h"
#include "../lrg-log.h"

struct _LrgListView
{
	LrgContainer parent_instance;
};

G_DEFINE_FINAL_TYPE (LrgListView, lrg_list_view, LRG_TYPE_CONTAINER)

static void
lrg_list_view_class_init (LrgListViewClass *klass)
{
}

static void
lrg_list_view_init (LrgListView *self)
{
}

LrgListView *
lrg_list_view_new (void)
{
	return g_object_new (LRG_TYPE_LIST_VIEW, NULL);
}
